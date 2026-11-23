# PixelMate on the M5Stack StopWatch

Bitsy on M5Stack's StopWatch (SKU C152): ESP32-S3R8 (16 MB flash, 8 MB octal
PSRAM), 1.75" round 466x466 CO5300 AMOLED with CST820 touch.

## Status: first milestone, not yet run on the device

| Part | State |
| --- | --- |
| Display (CO5300, QSPI 40 MHz) | written: 480x480 game sent 1:1, two frames overlap drawing and sending |
| Power-up (M5PM1 0x6E, M5IOE1 0x4F) | written: M5GFX's StopWatch sequence |
| Touch (CST820 0x15) | written; units with a CST9217 (0x5A) have no touch yet |
| Battery (M5PM1 VBAT/VIN) | written: voltage, estimated percent, USB present |
| Sound (ES8311 + AW8737A amp) | not yet: silent |
| Steps (BMI270 0x68) | not yet: runs as a desk pet (pats earn sparks) until then |
| Clock chip (RX8130 0x32) | not yet |
| Buttons (G1, G2, active low), vibration (IOE1 PWM) | not yet |

## Where the hardware details come from

M5Stack's pin table ([docs](https://docs.m5stack.com/en/core/StopWatch)) and
their MIT-licensed libraries: M5GFX `src/board_detect/m5/pmic_ops.hpp`
(`stopwatch_power_on`), `src/board_detect/m5/generated/esp32s3_board_data.hpp`
(pins, panel 468x468 at offset 6,0, touch range 0..233) and `src/M5GFX.cpp`
(`Panel_StopWatch` init commands); M5Unified `src/utility/power/M5PM1_Class.inl`
(VBAT/VIN registers). Things to check on the device first: the picture is
centred on the glass, touch lands where the finger is, and the QSPI clock can
go to M5's 80 MHz.

## Build

From this folder, with ESP-IDF 6.2 (no Board Manager step needed):

```
idf.py -B build -DIDF_TARGET=esp32s3 build
```

Back up the factory firmware before the first flash
(`esptool --port COMx read-flash 0 ALL stopwatch_factory.bin`).
