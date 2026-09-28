# ESP32-S3 Fan PID Experiment

A small PlatformIO + ESP-IDF learning project for understanding PID behavior on a real 4-wire PC fan. It alternates between 1800 and 2500 RPM and logs the controller signals for analysis.

## Hardware

- ESP32-S3 and Arctic Bionix F120 (4-wire fan).
- PWM on GPIO40 through an external open-collector, inverting transistor stage.
- Tachometer on GPIO39, external 10 kΩ pull-up, 2 pulses per revolution.
- PWM: 20 kHz, 10-bit (0..1023); nominal measurement/control interval: 500 ms.
- The fan is physically fixed to reduce vibration. **One blade is missing.**

Use the existing transistor interface and a tach pull-up compatible with ESP32-S3 input voltage. The firmware sets GPIO drive strength; it does not configure GPIO40 as open-drain.

## Control loop

Each iteration counts tachometer falling edges for a nominal 500 ms. At 2 pulses/revolution, `raw_rpm = pulses * 60` (60 RPM per count). An RPM EMA feeds the error calculation and derivative-on-measurement. A second EMA filters the derivative. Conditional integration prevents updates that push the requested output further beyond its limits.

The command is `40 + P + I + D`, clamped to 0..80%. Before output, it is truncated to a whole percentage and converted to inverted PWM duty. Targets change every 60 iterations (nominally 30 seconds). Switching occurs before measurement; the first high-target row is cycle 60. Timing uses `vTaskDelay`, not a deadline-based scheduler.

## Final settings

| Parameter | Value |
| --- | --- |
| KP / KI / KD | 0.04 / 0.003 / 0.003 |
| RPM_FILTER_ALPHA | 0.25 |
| RATE_FILTER_ALPHA | 0.35 |
| Targets | 1800 / 2500 RPM |
| Base command / maximum | 40% / 80% |

The RPM filter is seeded from the first sample. The first derivative is zero; the rate filter is seeded from the next derivative. The refactor preserves these initial conditions and the original arithmetic.

## CSV logging

At 115200 baud, `CSV_LOGGING=1` emits:

```text
time_ms,target_rpm,raw_rpm,filtered_rpm,rpm_rate,filtered_rate,error,p,i,d,power
```

`time_ms` is the ESP log timestamp in milliseconds. RPM and error use RPM; rates use RPM/s. `rpm_rate` is the derivative of **filtered RPM before the rate EMA**, not the derivative of raw RPM. P/I/D are command percentage points. `power` is the clamped floating-point command before whole-percent truncation, not measured electrical power. CSV rounding prevents exact reconstruction of PWM duty near integer boundaries.

The final recording is [data/pid_test_2026-09-28_12-02-10.csv](data/pid_test_2026-09-28_12-02-10.csv): 2,544 rows, 21.2 minutes, 42 target changes. Its original bytes are unchanged; it was moved from the repository root into `data/`.

## Build and run

Install PlatformIO with the ESP-IDF toolchain, then run from the project directory:

```sh
pio run
pio run --target upload --upload-port COM3
pio device monitor --baud 115200 --port COM3
```

Replace `COM3` with the board's port. The existing configuration is inconsistent: `platformio.ini` requests 16 MB / QIO, while the committed `sdkconfig` selects 2 MB / DIO. These settings were preserved. Confirm the actual board flash and align them before flashing; the build tools report the size mismatch.

To capture CSV, close the serial monitor, install `pyserial`, set `PORT` in `capture_serial.py`, then run `python capture_serial.py`. Start capture before resetting the board so it receives the CSV header. Logs are written to the current directory. The existing capture helper can open a headerless file if it detects a timestamp rollback without receiving a header; verify the header before analyzing a new capture.

## Report and reproduction

The [Ukrainian report](docs/pid_experiment_report_uk.pdf) contains separate RPM, derivative, P/I/D, and PWM graphs. Full-record plots retain every sample; fixed first-120-second views add detail without selecting favorable intervals.

```sh
python -m pip install -r analysis/requirements.txt
python analysis/plot_pid_data.py
```

This regenerates the PDF, `analysis/plots/*.png`, and `analysis/summary_metrics.csv`. It verifies the final CSV hash and basic data consistency. No extra smoothing is applied. Metrics separate the full recording from samples at least 10 seconds into each target segment (including the initial segment); this is a descriptive late-segment window, not proof of settling. The ±100 RPM band is descriptive. Both raw and filtered metrics are retained.

## Repository contents

- `src/main.c`: hardware setup, measurement, PID and CSV logging.
- `platformio.ini`, `CMakeLists.txt`, `sdkconfig.*`: original build configuration.
- `capture_serial.py`: original serial capture helper.
- `data/`: unchanged final measurement file.
- `analysis/`: reproducible report/plot script, dependencies and summary metrics.
- `docs/`: Ukrainian experimental report and verification notes.

## Experimental limitations

This is an educational experiment, not laboratory-grade RPM metrology. Tachometer quantization, noise, the 500 ms interval, filtering delay and the damaged fan limit interpretation. The controller repeatedly returns near both targets, with residual error. This recording contains no zero-RPM or output-saturation samples, so it does not demonstrate a motor power-off test or validate anti-windup during one.

Build verification and the local compiler-launcher limitation are recorded in [docs/verification.md](docs/verification.md).
