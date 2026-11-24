# PixelMate on the M5Stack StopWatch

Bitsy on M5Stack's StopWatch (SKU C152): ESP32-S3R8 (16 MB flash, 8 MB octal
PSRAM), 1.75" round 466x466 CO5300 AMOLED with CST820 touch.

## Status

| Part | State |
| --- | --- |
| Display (CO5300, QSPI 40 MHz) | the game's 480 px picture shown 1:1, centred on the round glass (`SW_VIEW_SIDE` in sw_video.h); the round layout (`pixelmate_feature(ROUND_SCREEN)`) keeps every screen inside the circle; runs on the device at 30 fps |
| Power-up (M5PM1 0x6E, M5IOE1 0x4F) | M5GFX's StopWatch sequence; runs on the device |
| Touch (CST820 0x15) | works, mapped 1:1 from measured taps (still logs each touch-down). CST9217 (0x5A) units: no touch yet |
| Sound (ES8311 + AW8737A) | starts on the device (M5Unified's codec setup) |
| Steps (BMI270 0x68) | walking pet; counts on the device |
| Clock chip (RX8130 0x32) | written: time survives power-off |
| KEYA (G2, yellow) | sleep / wake; on battery the pet goes on to pocket mode a second later |
| Pocket mode | deep sleep on battery with the step sensor still counting; the yellow button wakes it and the slept steps are added (checked on the device) |
| Battery (M5PM1) | voltage, estimated percent, USB present |
| Diary, acts, brain | same as the Mosaico (`../../brain/README.md`); not tried on this board yet |
| Not yet | magnetometer TOOLS (none on this board), KEYB (blue), vibration (the game has no haptics), the QSPI clock at M5's 80 MHz |

## Where the hardware details come from

M5Stack's pin table ([docs](https://docs.m5stack.com/en/core/StopWatch)) and
their MIT-licensed libraries: M5GFX `src/board_detect/m5/pmic_ops.hpp`
(`stopwatch_power_on`), `src/board_detect/m5/generated/esp32s3_board_data.hpp`
(pins, panel 468x468 at offset 6,0, touch range 0..233) and `src/M5GFX.cpp`
(`Panel_StopWatch` init commands); M5Unified `src/utility/power/M5PM1_Class.inl`
(VBAT/VIN registers).

## Build

From this folder, with ESP-IDF 6.2 (no Board Manager step needed):

```
idf.py -B build -DIDF_TARGET=esp32s3 build
```

Back up the factory firmware before the first flash
(`esptool --port COMx read-flash 0 ALL stopwatch_factory.bin`).
