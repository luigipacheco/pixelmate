# app_power_hw

Screen power and battery readout for ESP-Mosaico. Device-only; the Host
simulator never compiles it.

```c
app_power_hw_display(true, 30);      /* panel on, 30 % brightness */
app_power_hw_display(false, 0);      /* panel off; touch keeps working */

app_battery_reading_t battery;
if (app_power_hw_battery(&battery) == ESP_OK) {
    /* battery.percent 0..100, voltage_mv, current_ma (> 0 = charging) */
}
```

- **Display:** uses the Board Manager device `display_lcd` (CO5300 AMOLED).
  Off is `esp_lcd_panel_disp_on_off(false)`; on restores it and sets the
  brightness. `ESP_ERR_INVALID_STATE` until the board has created the display.
  The caller must stop submitting frames while the panel is off.
- **Battery:** reads the BQ27220 fuel gauge (I2C `0x55`) on the board's
  `i2c_master` bus with the standard commands StateOfCharge (`0x2C`),
  Voltage (`0x08`) and Current (`0x0C`). The bus is shared with touch and the
  IMU; the ESP-IDF I2C master driver serializes transactions. Each read is
  three short transfers with a 50 ms timeout, so poll it every few seconds.
- Both handles are borrowed from the board and kept for the app's lifetime.
  Call from the game loop task only.
