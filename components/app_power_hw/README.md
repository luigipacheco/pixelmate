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

**Why there is no screen control here:** an earlier version switched the
CO5300 panel off and changed its brightness from the game loop. Those panel
commands share the display bus with the board's frame-transfer task, which has
no hook for out-of-band commands, and the device could hang. Screen sleep and
dimming are now drawn instead (a black frame, darker colors); see
`app_game_runtime`.
