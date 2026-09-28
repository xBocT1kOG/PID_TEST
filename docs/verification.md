# Verification notes

Verified on 2026-09-28 against repository commit `ba4313a48dfbbe298e1c0f3234081002fe5038c2`.

## Source and data

- Compared executable C tokens for all four hardware helper functions: unchanged.
- Compared the complete `app_main` body after normalizing variable names and the extracted target constants: unchanged, except for making `dt` const and moving `fan_power` to its first useful assignment. Its unused initial assignment was removed. CSV header, formatting, initialization, operation order, switching, anti-windup and PWM truncation are preserved.
- All shared configuration defines retain their values. The unused `TARGET_RPM` define was replaced by names for the existing low/high literals and cycle count.
- `src/CMakeLists.txt` now explicitly lists `main.c` and declares `PRIV_REQUIRES driver`. Native ESP-IDF compilation exposed missing GPIO/LEDC header search paths without that dependency. This affects build dependencies, not control behavior.
- `platformio.ini`, committed `sdkconfig`, and `capture_serial.py` are unchanged.
- The final CSV matches the original Git blob byte for byte. `.gitattributes` disables newline conversion for `data/*.csv`.
- CSV SHA-256: `a8b722457a65949f4f3c5ffca99ba7362f67f8291a32b36995ffa53a16843479`.
- The report script checks the hash, column order, finite data, 500 ms sample spacing, targets, 60 RPM quantization, output range and consistency of logged error. It processes all 2,544 rows. `git diff --check` passed.

## Firmware build

Installed versions: PlatformIO Espressif32 6.13.0, ESP-IDF 5.5.3, Xtensa GCC 14.2.0 (esp-14.2.0_20251107), esptool 4.11.0.

The normal `pio run` path is **not verified successfully in this environment**: the installed `xtensa-esp32s3-elf-*` Windows launcher fails with `Failed to get path name. Error code: 5`, including when invoked with `--version`. The original source fails at the same compiler probe.

For compilation verification, a scratch copy with identical source and committed sdkconfig was configured through PlatformIO. Its generated native ESP-IDF CMake targets were built using the underlying `xtensa-esp-elf-*` tools directly, with `XTENSA_GNU_CONFIG` selecting `xtensa_esp32s3.so` and `-mdynconfig=xtensa_esp32s3.so` selecting the matching linker libraries. The esptool Python dependencies were supplied in a scratch directory. No installed compiler binaries or delivered build settings were changed.

Both native targets completed successfully:

- Application: compiled, linked and converted to an ESP32-S3 image. Size `0x32360` (205,664 bytes); fits the `0x100000` application partition, with 80% free.
- Bootloader: compiled, linked and converted to an ESP32-S3 image. Size `0x5160` (20,832 bytes); size check passed, with 36% free.

This verifies compilation and linking through the installed ESP-IDF toolchain, not an unmodified end-to-end PlatformIO build or on-device behavior. No firmware was uploaded and no hardware run was performed.

## Existing configuration limitation

`platformio.ini` requests 16 MB flash and QIO, while `sdkconfig.esp32-s3-devkitc-1` selects 2 MB and DIO. The build reports a flash-size mismatch. Both original files were preserved; confirm the actual board and align its flash configuration before uploading. The verification images used the committed sdkconfig's 2 MB/DIO settings and are not included as flash-ready deliverables.

## Report

The generator completed and produced five A4 pages. Every page was rendered and visually inspected for readable Ukrainian text, charts, legends, tables and footers. The hash on the final page was rechecked after restoring the Git blob's original line endings. All required plots use the supplied CSV with no added smoothing. The two detailed views use the fixed first 120 seconds. Full and late-segment metrics retain separate raw/filtered results; the late-segment rule excludes the first 10 seconds of every segment, including startup.
