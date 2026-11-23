# PixelMate on the M5Stack StopWatch

Bitsy on M5Stack's StopWatch (SKU C152): ESP32-S3R8 (16 MB flash, 8 MB octal
PSRAM), 1.75" round 466x466 CO5300 AMOLED with CST820 touch.

## Status

| Part | State |
| --- | --- |
| Display (CO5300, QSPI 40 MHz) | game scaled to 328 px and centred so the whole square fits the round glass (`SW_VIEW_SIDE` in sw_video.h); runs on the device |
| Power-up (M5PM1 0x6E, M5IOE1 0x4F) | M5GFX's StopWatch sequence; runs on the device |
| Touch (CST820 0x15) | answers; mapping not calibrated yet (logs raw values on each touch-down). CST9217 (0x5A) units: no touch yet |
| Sound (ES8311 + AW8737A) | written (M5Unified's codec setup), not heard yet |
| Steps (BMI270 0x68) | written: walking pet, TOOLS motion apps |
| Clock chip (RX8130 0x32) | written: time survives power-off |
| KEYA (G2) | sleep / wake, like the Mosaico's AI button |
| Battery (M5PM1) | voltage, estimated percent, USB present |
| Not yet | pocket-mode deep sleep, magnetometer TOOLS (none on this board), KEYB, vibration (the game has no haptics) |

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
