# app_power_hw

Battery readout for ESP-Mosaico. Device-only; the Host simulator never
compiles it.

```c
app_battery_reading_t battery;
if (app_power_hw_battery(&battery) == ESP_OK) {
    /* battery.percent 0..100, voltage_mv, current_ma (> 0 = charging) */
}
```

- Reads the BQ27220 fuel gauge (I2C `0x55`) on the board's `i2c_master` bus
  with the standard commands StateOfCharge (`0x2C`), Voltage (`0x08`) and
  Current (`0x0C`), plus RemainingCapacity (`0x10`), FullChargeCapacity
  (`0x12`), AverageCurrent (`0x14`) and TimeToEmpty (`0x16`) for power
  measurements (each reported as unknown if its read fails). The bus is shared with touch and the IMU; the ESP-IDF I2C
  master driver serializes transactions. Each read is seven short transfers
  with a 50 ms timeout, so poll it every few seconds.
- The gauge device handle is kept for the app's lifetime. Call from the game
  loop task only.
- **Battery profile.** The BQ27220 keeps its battery profile in RAM. When the
  gauge resets (the battery ran flat, or a hard power-off), it falls back to
  factory defaults that don't match the 80 mAh cell, and the percentage reads
  wrong. Vibe Mode loads the profile at every boot through the BSP, which is
  why "Vibe Mode and back" used to fix it. Now the first
  `app_power_hw_battery()` call starts a one-shot task that reads
  DesignCapacity (`0x3C`). If it isn't 80 mAh, the task loads the BSP's exact
  profile through Espressif's `bq27220` driver (verify, CONFIG UPDATE write,
  verify, seal). It's identical to Vibe Mode's, so the two firmwares never
  rewrite each other's. Reads return `ESP_ERR_NOT_FINISHED` until the task is
  done (a few seconds when it loads). `app_power_hw_profile()` and
  `/api/stats` (`gauge_profile`, `power_design_mah`) show the outcome.

**Why there is no screen control here:** an earlier version switched the
CO5300 panel off and changed its brightness from the game loop. Those panel
commands share the display bus with the board's frame-transfer task, which has
no hook for out-of-band commands, and the device could hang. Screen sleep and
dimming are now drawn instead (a black frame, darker colors); see
`app_game_runtime`.
