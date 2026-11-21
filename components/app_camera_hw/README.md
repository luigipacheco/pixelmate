# app_camera_hw

The OV3640 camera module in ESP-Mosaico's **left** slot as a 128x128
luminance feed for the photo booth (the HI RES lens; lower lenses are
box-averaged from it). Device-only; the Host simulator never compiles it.

```c
app_camera_hw_set_mode(APP_CAMERA_MODE_PROBE);    /* every tick: is a camera plugged in? */
bool plugged = app_camera_hw_present();
app_camera_hw_set_mode(APP_CAMERA_MODE_STREAM);   /* booth shown: stream while present */
uint32_t seq = 0;
uint8_t luma[APP_PHOTO_MAX_PIXELS];
if (app_camera_hw_state() == APP_CAMERA_LIVE && app_camera_hw_frame(luma, &seq)) {
    /* a new upright 128x128 frame */
}
app_camera_hw_set_mode(APP_CAMERA_MODE_OFF);      /* screen asleep: release, stop probing */
```

## Why not the BSP camera component

`esp-mosaico-bsp`'s `mosaico_module_camera` claims the slot through
`mosaico_module_mgr`, which gets its bus from `bsp_subboard_get_i2c_bus()` →
`bsp_i2c_init()` → `i2c_new_master_bus()` on I2C port 0. Raylib Lite Engine's
board adapter already owns that port through Board Manager (`i2c_master`), so
the BSP path would collide on a v1.0 board. This component reuses the same
esp_video/esp_cam_sensor stack and copies only the slot sequence it needs
(credited in the source); neither the BSP nor the engine is modified.

## Board details

- **Bus.** The hardware revision comes from eFuse `USER_DATA` (as the BSP and
  the engine read it). v1.0 (`0x0100`): the slots share the mainboard bus, so
  the Board Manager `i2c_master` handle is borrowed (`esp_board_periph_ref_handle`,
  never released). v1.1/v1.2: the slots have their own bus, created here on
  I2C1, SDA GPIO0 / SCL GPIO1, and kept for the app's lifetime.
- **Detection and hot-plug.** The camera is not needed at boot. In PROBE and
  STREAM modes the worker checks the slot about once a second: while absent,
  GPIO14 is driven low (left-slot address select) and the EEPROM at `0x50` must
  read `"ESP"` + board type `0x07` (camera); while present a plain address probe
  is enough (the camera EEPROM answers at `0x50` whatever GPIO14 does, so this
  works while GPIO14 is camera D4). Two misses in a row (about 1-2 s) mean
  unplugged. The camera is brought up only once it is detected and only in
  STREAM mode. On removal, on PROBE/OFF, or after a capture failure, the
  worker releases everything: STREAMOFF (all buffers are already requeued),
  unmap and free the buffers, close `/dev/video2`, `esp_video_deinit`, GPIO14
  back to address select, PWDN high, flash off. A replug or a mode change
  retries after an error. This mirrors the BSP's `module_slot_scan` example
  without its module manager.
- **Pins (left slot).** D0..D7 = 16, 15, 33, 4, 14, 12, 18, 13; VSYNC 55,
  DE 19, PCLK 17, RESET 53, PWDN 48; no XCLK (the board has its own
  oscillator; 20 ms settle before SCCB). SCCB at 100 kHz on the slot bus.
  The flash LED gate (GPIO34, active low) is held off.
- **USB-Serial/JTAG.** D2 is GPIO33, the built-in USB-Serial/JTAG pad. The app
  disables that controller (`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG` off in
  `sdkconfig.defaults`) and the component also turns the pad and its clock off
  before bring-up. The console stays on UART and ESP-Iris keeps High-Speed USB.
- **Power.** The slots' 3V3 rail (GPIO60, active low) is the LCD rail the board
  already keeps on, so it is not switched here; a released sensor is held in
  power-down (PWDN high) and every start runs the full esp_video bring-up.
- **ESP-IDF.** The BSP's DVP stability patch
  (`mosaico_module_camera/patches/esp-idf-dvp-frame-capture-stability.patch`)
  was applied to the pinned checkout once by hand, because this component does
  not run the BSP's CMake hook: it adds a 64-byte DMA frame-tail descriptor,
  resets the camera FIFO at each frame start and drops short frames.

## Feed

- Format: the sensor's Kconfig default, UYVY 1024x768 at 25 fps, 2 mmap
  buffers (about 3 MB of PSRAM). Every dequeued buffer is requeued at once.
- Each frame is center-cropped to 768x768 and reduced to 128x128 by averaging
  four luminance samples per 6x6 block (16 KB, in PSRAM). The sensor is mounted sideways; like the
  BSP example's PPA setup (rotate 270° CCW, then mirror Y) the output is
  `out(x, y) = in(S-1-y, S-1-x)`.
- The first two frames after each start are dropped (exposure settling) before
  the state turns `LIVE`. Five consecutive frame timeouts give `ERROR`.

## Ownership and threading

A worker task (`camera`, priority 2, 4 KB stack), created on the first mode
other than OFF, owns the slot bus probes, the device, its buffers and every
ioctl. Callers only set an atomic mode (the task is notified on changes), read
the atomic presence and state, and copy the newest frame under a mutex. In OFF
the task blocks without probing; presence keeps its last value. Call
`app_camera_hw_set_mode()` from the game loop only.
