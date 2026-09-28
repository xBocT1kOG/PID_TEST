import serial
from datetime import datetime

PORT = "COM3"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)

log_file = None
last_time_ms = None


def open_new_log():
    global log_file

    if log_file:
        log_file.close()

    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = f"pid_test_{timestamp}.csv"

    log_file = open(filename, "w", encoding="utf-8", newline="")
    print(f"\n--- New log: {filename} ---")


try:
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").strip()

        if not line:
            continue

        print(line)

        # заголовок CSV
        if line.startswith("time_ms,"):
            open_new_log()
            log_file.write(line + "\n")
            log_file.flush()
            last_time_ms = None
            continue

        # пробуем взять time_ms
        try:
            time_ms = int(float(line.split(",")[0]))
        except (ValueError, IndexError):
            continue

        # если время откатилось назад — reset платы
        if last_time_ms is not None and time_ms < last_time_ms:
            open_new_log()

        last_time_ms = time_ms

        if log_file:
            log_file.write(line + "\n")
            log_file.flush()

except KeyboardInterrupt:
    print("\nLogging stopped.")

finally:
    if log_file:
        log_file.close()

    ser.close()