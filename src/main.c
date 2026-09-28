#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// PWM hardware: the external open-collector transistor inverts the signal.
#define FAN_PWM_GPIO       GPIO_NUM_40
#define PWM_FREQUENCY      20000
#define PWM_RESOLUTION     LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY       1023
#define PWM_TIMER          LEDC_TIMER_0
#define PWM_CHANNEL        LEDC_CHANNEL_0
#define PWM_MODE           LEDC_LOW_SPEED_MODE

// Tachometer: two falling edges per revolution; nominal 500 ms window.
#define FAN_TACH_GPIO      GPIO_NUM_39
#define CONTROL_TIME_MS    500

// Alternate targets every 60 control cycles (nominally 30 seconds).
#define TARGET_LOW_RPM     1800.0f
#define TARGET_HIGH_RPM    2500.0f
#define TARGET_HOLD_CYCLES 60
#define BASE_POWER         40.0f  // Fixed bias, in percentage points.
#define MAX_FAN_POWER      80.0f  // Upper controller command limit (%).

// Final experimental gains; integral uses seconds, rate uses RPM/s.
#define KP                0.04f
#define KI                0.003f
#define KD                0.003f
#define RPM_FILTER_ALPHA  0.25f
#define RATE_FILTER_ALPHA 0.35f
#define CSV_LOGGING       1

static const char *TAG = "FAN";
static volatile uint32_t tach_pulses = 0; // ISR count for the current window.

static void IRAM_ATTR tach_isr_handler(void *arg)
{
    tach_pulses++;
}

static void fan_pwm_init(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = PWM_MODE,
        .timer_num = PWM_TIMER,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz = PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = FAN_PWM_GPIO,
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    // Set drive strength only; open-collector operation is external hardware.
    ESP_ERROR_CHECK(gpio_set_drive_capability(FAN_PWM_GPIO, GPIO_DRIVE_CAP_0));
    ESP_LOGI(TAG, "PWM initialized: GPIO=%d, frequency=%d Hz",
             FAN_PWM_GPIO, PWM_FREQUENCY);
}

// Convert whole-percent fan command to inverted 10-bit MCU PWM duty.
static void fan_set_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = PWM_MAX_DUTY - (PWM_MAX_DUTY * percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(PWM_MODE, PWM_CHANNEL));
}

static void tach_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FAN_TACH_GPIO),
        .mode = GPIO_MODE_INPUT,
        // The tachometer input uses an external 10 kohm pull-up.
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(FAN_TACH_GPIO, tach_isr_handler, NULL));
}

void app_main(void)
{
    fan_pwm_init();
    tach_init();

    float integral = 0.0f; // Accumulated error in RPM * seconds.
    float target_rpm = TARGET_LOW_RPM;
    int cycles = 0;
    const float dt = CONTROL_TIME_MS / 1000.0f;
    float filtered_rpm = 0.0f;
    bool rpm_filter_initialized = false;
    float previous_filtered_rpm = 0.0f;
    bool first_rate_measurement = true;
    float filtered_rpm_rate = 0.0f;
    bool rate_filter_initialized = false;

#if CSV_LOGGING
    printf("time_ms,target_rpm,raw_rpm,filtered_rpm,"
           "rpm_rate,filtered_rate,error,p,i,d,power\n");
#endif

    while (1) {
        // Preserve switching before measurement: first high target is cycle 60.
        cycles++;
        if (cycles == TARGET_HOLD_CYCLES) {
            target_rpm = TARGET_HIGH_RPM;
        }
        if (cycles == 2 * TARGET_HOLD_CYCLES) {
            target_rpm = TARGET_LOW_RPM;
            cycles = 0;
        }

        // Preserve the original reset / delay / read measurement window.
        tach_pulses = 0;
        vTaskDelay(pdMS_TO_TICKS(CONTROL_TIME_MS));
        uint32_t pulses = tach_pulses;
        // RPM = pulses * 60 s/min / (2 pulses/rev * 0.5 s) = pulses * 60.
        float raw_rpm = pulses * 60.0f;

        // Seed the EMA from the first sample to avoid an artificial zero ramp.
        if (!rpm_filter_initialized) {
            filtered_rpm = raw_rpm;
            rpm_filter_initialized = true;
        } else {
            filtered_rpm = RPM_FILTER_ALPHA * raw_rpm
                         + (1.0f - RPM_FILTER_ALPHA) * filtered_rpm;
        }

        // Derivative-on-measurement avoids a direct target-step derivative kick.
        // rpm_rate is derived from filtered RPM, before the second EMA.
        float rpm_rate = 0.0f;
        if (!first_rate_measurement) {
            rpm_rate = (filtered_rpm - previous_filtered_rpm) / dt;
            if (!rate_filter_initialized) {
                filtered_rpm_rate = rpm_rate;
                rate_filter_initialized = true;
            } else {
                filtered_rpm_rate = RATE_FILTER_ALPHA * rpm_rate
                                  + (1.0f - RATE_FILTER_ALPHA) * filtered_rpm_rate;
            }
        } else {
            first_rate_measurement = false;
        }
        previous_filtered_rpm = filtered_rpm;

        float error = target_rpm - filtered_rpm;
        float p_term = KP * error;              // Immediate error correction.
        float d_term = -KD * filtered_rpm_rate; // Oppose measured speed changes.

        // Trial integration: reject only updates that push beyond a limit.
        float new_integral = integral + error * dt;
        float new_i_term = KI * new_integral;
        float requested_power = BASE_POWER + p_term + new_i_term + d_term;
        bool windup_high = (requested_power > MAX_FAN_POWER) && (error > 0);
        bool windup_low = (requested_power < 0.0f) && (error < 0);
        if (!windup_high && !windup_low) {
            integral = new_integral;
        }
        float i_term = KI * integral; // Use the accepted integral state.

        float fan_power = BASE_POWER + p_term + i_term + d_term;
        // Clamp the biased PID command to the experimental 0..80% range.
        if (fan_power > MAX_FAN_POWER) {
            fan_power = MAX_FAN_POWER;
        }
        if (fan_power < 0.0f) {
            fan_power = 0.0f;
        }

        // Preserve integer-percent truncation; CSV retains the float command.
        fan_set_percent((uint8_t)fan_power);

#if CSV_LOGGING
        printf("%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
               "%.3f,%.3f,%.3f,%.2f\n",
               (unsigned long)esp_log_timestamp(), target_rpm, raw_rpm,
               filtered_rpm, rpm_rate, filtered_rpm_rate, error,
               p_term, i_term, d_term, fan_power);
#else
        ESP_LOGI("PID", "Target: %.0f | Raw: %.0f | Filtered: %.0f | "
                 "dRPM/dt: %.0f | Filt dRPM/dt: %.0f | "
                 "Err: %.0f | P: %.2f | I: %.2f | D: %.2f | Power: %.1f%%",
                 target_rpm, raw_rpm, filtered_rpm, rpm_rate,
                 filtered_rpm_rate, error, p_term, i_term, d_term, fan_power);
#endif
    }
}
