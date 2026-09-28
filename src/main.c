#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "driver/ledc.h"
#include "driver/gpio.h"


#define FAN_PWM_GPIO      GPIO_NUM_40

#define PWM_FREQUENCY     20000
#define PWM_RESOLUTION    LEDC_TIMER_10_BIT
#define PWM_TIMER         LEDC_TIMER_0
#define PWM_CHANNEL       LEDC_CHANNEL_0
#define PWM_MODE          LEDC_LOW_SPEED_MODE

#define PWM_MAX_DUTY      1023

#define FAN_TACH_GPIO GPIO_NUM_39

#define TARGET_RPM      1800
#define MAX_FAN_POWER   80.0f

#define CONTROL_TIME_MS 500

#define BASE_POWER 40.0f

#define KP              0.04f
#define KI              0.003f
#define KD              0.003f

#define RPM_FILTER_ALPHA 0.25f
#define RATE_FILTER_ALPHA 0.35f

#define CSV_LOGGING 1

static volatile uint32_t tach_pulses = 0;

static const char *TAG = "FAN";

static void IRAM_ATTR tach_isr_handler(void *arg)
{
    tach_pulses++;
}

// ============================================================
// Инициализация PWM
// ============================================================

static void fan_pwm_init(void)
{
    // ---------------------------------------------
    // 1. Настраиваем TIMER
    // ---------------------------------------------

    ledc_timer_config_t timer_config = {

        .speed_mode = PWM_MODE,

        .timer_num = PWM_TIMER,

        .duty_resolution = PWM_RESOLUTION,

        .freq_hz = PWM_FREQUENCY,

        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(
        ledc_timer_config(&timer_config)
    );


    // ---------------------------------------------
    // 2. Настраиваем CHANNEL
    // ---------------------------------------------

    ledc_channel_config_t channel_config = {

        .gpio_num = FAN_PWM_GPIO,

        .speed_mode = PWM_MODE,

        .channel = PWM_CHANNEL,

        .timer_sel = PWM_TIMER,

        .duty = 0,

        .hpoint = 0
    };

    ESP_ERROR_CHECK(
        ledc_channel_config(&channel_config)
    );


    // ---------------------------------------------
    // 3. Переводим GPIO40 в OPEN-DRAIN
    // ---------------------------------------------

    ESP_ERROR_CHECK(
    gpio_set_drive_capability(
        FAN_PWM_GPIO,
        GPIO_DRIVE_CAP_0
    )
);

    ESP_LOGI(
        TAG,
        "PWM initialized: GPIO=%d, frequency=%d Hz",
        FAN_PWM_GPIO,
        PWM_FREQUENCY
    );
}


// ============================================================
// Установка PWM в процентах
// ============================================================

static void fan_set_percent(uint8_t percent)
{
    if (percent > 100)
    {
        percent = 100;
    }


    uint32_t duty =
        PWM_MAX_DUTY - (PWM_MAX_DUTY * percent) / 100;


    ESP_ERROR_CHECK(
        ledc_set_duty(
            PWM_MODE,
            PWM_CHANNEL,
            duty
        )
    );


    ESP_ERROR_CHECK(
        ledc_update_duty(
            PWM_MODE,
            PWM_CHANNEL
        )
    );
}

static void tach_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FAN_TACH_GPIO),

        .mode = GPIO_MODE_INPUT,

        // Внешний 10k уже делает pull-up
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        // Считаем один фронт каждого импульса
        .intr_type = GPIO_INTR_NEGEDGE
    };

    ESP_ERROR_CHECK(
        gpio_config(&io_conf)
    );

    ESP_ERROR_CHECK(
        gpio_install_isr_service(0)
    );

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            FAN_TACH_GPIO,
            tach_isr_handler,
            NULL
        )
    );
}

// ============================================================
// MAIN
// ============================================================

void app_main(void)
{
    // ---------------------------------------------
    // Инициализация железа
    // ---------------------------------------------

    fan_pwm_init();
    tach_init();


    // ---------------------------------------------
    // Состояние регулятора
    // ---------------------------------------------

    float fan_power = BASE_POWER;

    float integral = 0.0f;

    float target_rpm = 1800.0f;

    int cycles = 0;


    // dt = 500 ms = 0.5 s
    float dt = CONTROL_TIME_MS / 1000.0f;
    float filtered_rpm = 0.0f;
    bool rpm_filter_initialized = false;
    float previous_filtered_rpm = 0.0f;
    bool first_rate_measurement = true;
    float filtered_rpm_rate = 0.0f;
    bool rate_filter_initialized = false;

    #if CSV_LOGGING
    printf(
    "time_ms,target_rpm,raw_rpm,filtered_rpm,"
    "rpm_rate,filtered_rate,error,p,i,d,power\n"
    );
    #endif

    while (1)
    {
        cycles++;


        // =============================================
        // Меняем TARGET для нашего эксперимента
        // =============================================

        if (cycles == 60)
        {
            target_rpm = 2500.0f;

        }


        if (cycles == 120)
        {
            target_rpm = 1800.0f;

            cycles = 0;
        }


        // =============================================
        // 1. Измеряем RPM
        // =============================================

        tach_pulses = 0;

        vTaskDelay(
            pdMS_TO_TICKS(CONTROL_TIME_MS)
        );

        uint32_t pulses = tach_pulses;


        // Окно 0.5 секунды
        // 2 TACH импульса на оборот
        //
        // RPM = pulses * 60

        float rpm = pulses * 60.0f;

        // =============================================
        // 1.5. RPM FILTER
        // =============================================

        if (!rpm_filter_initialized)
        {
            filtered_rpm = rpm;
            rpm_filter_initialized = true;
        }
        else
        {
            filtered_rpm =
                RPM_FILTER_ALPHA * rpm
                + (1.0f - RPM_FILTER_ALPHA) * filtered_rpm;
        }

        float rpm_rate = 0.0f;

        if (!first_rate_measurement)
        {
            rpm_rate =
                (filtered_rpm - previous_filtered_rpm) / dt;

    // Фильтруем "акселерометр"
        if (!rate_filter_initialized)
        {
            filtered_rpm_rate = rpm_rate;
            rate_filter_initialized = true;
        }
        else
        {
            filtered_rpm_rate =
            RATE_FILTER_ALPHA * rpm_rate
            + (1.0f - RATE_FILTER_ALPHA) * filtered_rpm_rate;
    }
}
else
{
    first_rate_measurement = false;
}

previous_filtered_rpm = filtered_rpm;
        // =============================================
        // 2. ERROR
        // =============================================

        float error =
            target_rpm - filtered_rpm;


        // =============================================
        // 3. P
        // =============================================

        float P =
            KP * error;


        // =============================================
        // 4. D
        // =============================================

        float D = -KD * filtered_rpm_rate;


        // =============================================
        // 5. I + ANTI-WINDUP
        // =============================================

        // Пока НЕ изменяем настоящий integral.
        // Сначала проверяем, что получилось бы.

        float new_integral =
            integral + error * dt;


        float new_I =
            KI * new_integral;


        // Что PID ХОТЕЛ БЫ выдать,
        // если разрешить интегратору измениться.

        float requested_power =
            BASE_POWER
            + P
            + new_I
            + D;


        // Если уже хотим выйти выше максимума
        // И ошибка толкает нас ещё выше:
        //
        // интегратор НЕ накапливаем.

        bool windup_high =
            (requested_power > MAX_FAN_POWER)
            &&
            (error > 0);


        // Аналогично для нижнего ограничения.

        bool windup_low =
            (requested_power < 0.0f)
            &&
            (error < 0);


        if (!windup_high && !windup_low)
        {
            integral = new_integral;
        }


        // Теперь вычисляем НАСТОЯЩУЮ I
        // после решения anti-windup.

        float I =
            KI * integral;


        // =============================================
        // 6. PID OUTPUT
        // =============================================

        fan_power =
            BASE_POWER
            + P
            + I
            + D;


        // =============================================
        // 7. Ограничение мощности
        // =============================================

        if (fan_power > MAX_FAN_POWER)
        {
            fan_power = MAX_FAN_POWER;
        }


        if (fan_power < 0.0f)
        {
            fan_power = 0.0f;
        }


        // =============================================
        // 8. Отправляем PWM
        // =============================================

        fan_set_percent(
            (uint8_t)fan_power
        );


        // =============================================
        // 9. LOG
        // =============================================
        #if CSV_LOGGING
            printf(
                "%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
                "%.3f,%.3f,%.3f,%.2f\n",
                (unsigned long)esp_log_timestamp(),
                target_rpm,
                rpm,
                filtered_rpm,
                rpm_rate,
                filtered_rpm_rate,
                error,
                P,
                I,
                D,
                fan_power
            );
        #else
            ESP_LOGI(
                "PID",
                "Target: %.0f | Raw: %.0f | Filtered: %.0f | "
                "dRPM/dt: %.0f | Filt dRPM/dt: %.0f | "
                "Err: %.0f | P: %.2f | I: %.2f | D: %.2f | Power: %.1f%%",
                target_rpm,
            rpm,
            filtered_rpm,
            rpm_rate,
            filtered_rpm_rate,
            error,
            P,
            I,
            D,
            fan_power
            );
        #endif
    }
}