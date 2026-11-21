# app_pedometer_hw

Turns on the BMI270's built-in step counter and reads its cumulative count.
The sensor counts by itself, so steps are counted while the game is idle,
dimmed or busy, without the application sampling the accelerometer.

Depends on the board's Board Manager device `imu_sensor` (created by the
ESP-Mosaico board adapter) and the `bmi270` driver. Device-only; the Host
simulator never compiles it.

```c
if (app_pedometer_hw_start() == ESP_OK) {
    uint32_t steps = 0;
    if (app_pedometer_hw_read(&steps) == ESP_OK) {
        /* cumulative since boot; credit the increase */
    }
}
```

- `app_pedometer_hw_start()` returns `ESP_ERR_INVALID_STATE` while the board
  has not created the IMU (retry later) and `ESP_FAIL` if the sensor rejects
  the feature. It is idempotent after success.
- `app_pedometer_hw_read()` returns the count since the sensor was configured.
  It restarts at 0 on every boot, so callers keep their own running total.
- Call both from the game loop task only. The sensor handle is borrowed from
  the board and never freed here.

**Coupling note:** the board's device handle is a private struct whose first
member is `struct bmi2_dev`; this component relies on that layout (see
`examples/boards/esp-mosaico/mosaico_setup_imu.c` in Raylib Lite Engine).
Re-check it when updating the engine submodule.
