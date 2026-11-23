# M5Stack StopWatch (C152, ESP32-S3R8): 1.75" round AMOLED watch.
# First milestone: picture and touch. It has a BMI270 (so it can become a
# walking pet once a driver for its bus exists), an RX8130 clock, an ES8311
# codec and a vibration motor, all still "none" here.
pixelmate_name("M5 STOPWATCH")
pixelmate_part(CAMERA none)
pixelmate_part(INTERACT none)
pixelmate_part(NECK none)
pixelmate_part(NET plain)
pixelmate_part(PEDOMETER none)
pixelmate_part(POWER m5pm1)
pixelmate_part(RTC none)
pixelmate_part(SENSORS none)
pixelmate_part(SLEEP none)
pixelmate_feature(DESK_PET)   # Until the step counter is wired up.
