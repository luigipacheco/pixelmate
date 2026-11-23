# M5Stack StopWatch (C152, ESP32-S3R8): 1.75" round AMOLED watch, a walking
# pet like the ESP-Mosaico. Its chips sit on the internal I2C bus (port 1),
# which the board adapter opens.
pixelmate_name("M5 STOPWATCH")
pixelmate_setting(I2C_PORT 1)
pixelmate_setting(SLEEP_BUTTON_GPIO 2)   # KEYA (yellow), active low.
pixelmate_part(CAMERA none)
pixelmate_part(INTERACT none)
pixelmate_part(NECK none)
pixelmate_part(NET plain)
pixelmate_part(PEDOMETER bmi270)
pixelmate_part(POWER m5pm1)
pixelmate_part(RTC rx8130)
pixelmate_part(SENSORS none)
pixelmate_part(SLEEP none)
pixelmate_feature(ROUND_SCREEN)
