# PixelMate: meet Bitsy, a walking pet

**PixelMate** turns your ESP-Mosaico into **Bitsy**, a pixel-art robot pet that
lives on your steps. Walk and Bitsy gets fed; skip a day and it gets hungry.
Every 20 steps earns a **spark**, and sparks buy everything else: six arcade
games, eye colors, pixel styles, animations, a photo booth with retro looks
and even a Wi-Fi webcam.

It is inspired by the pocket step-counter pets of the '90s, with an original
character: just a pair of expressive pixel eyes.

Made by **Luis Pacheco**: [www.luigipacheco.com](https://www.luigipacheco.com)

## What you can do

- **Bitsy** reacts to everything: blinks, follows your finger, gets happy, sleepy, hungry, surprised or grumpy. Tap the eyes to pet it.
- **Steps ring** with today's progress, your all-time total and Bitsy's health (5 hearts). The BMI270's own step counter keeps counting while the screen sleeps, even in deep sleep.
- **Arcade**: six pixel cabinets in two rows (Spark Slots, Spark Stack, Spark Grid / Bit Flip, Bitsy Says, HI-LO), each with an attract-mode demo. Buy them in order with sparks, then play for more.
- **Spark Grid**: a 5×5 logic puzzle. Row and column hints tell you the totals and how many duds hide in each line. Flip x2 and x3 tiles to multiply your score, CASH to bank it, and clear boards to climb from LV 1 to LV 5.
- **Bitsy Says**: Bitsy's eyes take the color of each pad as it flashes a growing sequence. Repeat it back.
- **TOOLS**: a Flipper-style page of sensor apps: live SENSORS graphs, STEP CAL, COMPASS and metal detector, LEVEL, SEISMO and a WI-FI scanner. Using them levels Bitsy up.
- **Photo booth** (needs a camera module): low-res photos and GIFs in black and white or **COLOR FILM**, with retro looks. Plug the camera in or out at any time.
- **Clock mode**: on USB power, when Bitsy dozes off the screen becomes a dim pixel clock, with Bitsy's eyes as the blinking colon, the date and today's steps.

## The arcade

Every game is bought in the store, in this order. Each play costs a few sparks
and good play pays them back with interest.

| Game | Unlock | A play | Pays |
|---|---|---|---|
| **Spark Slots**: stop each of the three reels yourself | 100 | 3 | 7-7-7 pays 150. Pure luck, with a house edge. |
| **Spark Stack**: drop a sliding bar on the tower; the overhang is cut off | 150 | 5 | Only reaching the top (12 rows) pays: 20, +2 per perfect drop, up to 40. |
| **Spark Grid**: flip tiles, dodge the duds | 200 | 10 | The score multiplies with each flip. Level 1 boards pay 24 to 54, level 5 up to 576. A dud or CASH drops you back to the number of tiles you flipped. Long-press a tile for memo marks. |
| **Bit Flip**: set 8 switches to the target number in binary before the timer runs out | 250 | 5 | 1 per hit, up to 30. The timer gets shorter with every hit. |
| **Bitsy Says**: repeat the color sequence | 300 | 5 | 1 per round, up to 30. |
| **HI-LO**: higher or lower, double or nothing | 400 | 5 | Each right guess doubles the pot; cash out any time (automatically after 5 wins). |

## Photo booth and webcam

Plug an OV3640 or SC101IOT camera board into the **left** slot and a Booth page
appears after TOOLS (buy PHOTO BOOTH for 300 sparks). Tap **SNAP** for a 3-2-1
photo or **GIF** for an 8-frame clip; **ROLL** browses and deletes them.

- **Lenses:** LO RES 32×32 is free; MID RES 64×64 and HI RES 128×128 are sold.
- **Film:** black and white in the pet's palette, or **COLOR FILM** for real
  16-color photos.
- **Looks:** PLAIN, DITHER, SCANLINES, GAME-GREEN, HALFTONE, THERMAL, ASCII,
  GLITCH (VHS tears), EDGES (glowing outlines), POP (poster colors), MIRROR
  (symmetric selfies) and CRT (old TV stripes).
- The ESP-Mosaico keeps up to 300 photos and GIFs on 4 MB of its own (the
  StackChan and StopWatch 12 photos and 2 GIFs), and the stats page has a
  gallery to save them as PNG or GIF.

**Wi-Fi webcam:** buy **WEBCAM** in the store, open the booth and tap
**ON AIR**. The screen shows a 4-digit code; open
`http://pixelmate.local:81/cam?k=CODE` on a computer on the same network. For
Zoom or Meet, add that address to OBS Studio as a Media Source (or Browser
source), click **Start Virtual Camera** and pick **OBS Virtual Camera** in your
call app. The stream is small and retro (about 10 frames a second), and it only
runs while ON AIR is lit.

## TOOLS and the Interaction module

The TOOLS page (after the arcade) turns the board's sensors into little apps.
Each use earns Bitsy tools XP, and it levels up every 10.

- **SENSORS:** live graphs of the accelerometer, gyroscope, magnetometer,
  battery voltage and current, and steps.
- **STEP CAL:** walk 50 counted steps to correct the step counter (saved).
- **COMPASS / METAL:** uses the board's magnetometers; the metal detector beeps
  faster near metal.
- **LEVEL** (a bubble level, tap to zero), **SEISMO** (a vibration trace) and a
  **WI-FI** network scanner.

With the **Interaction module** in the **right** slot, three more tools appear:
**IR REMOTE** (power, volume, channel and a TV-OFF blast), **GUARD** (a motion
alarm that counts visitors) and **LIGHT** (a light meter and a lamp). The
module's LEDs glow with Bitsy's mood, walking past wakes Bitsy, and its two
keys work as arcade buttons.

## Made for every day

- **Feed** Bitsy snacks (CELL, CHIP, MEGA) with sparks. To stay healthy it
  needs half your daily goal in steps; fed days add a heart, missed days take
  one, and at zero Bitsy faints until you walk again.
- **Style:** eye colors, FINE or SMOOTH pixels, and animations: a spark trail
  as you walk, a dance every 100 steps, star eyes and rainbow eyes.
- **Sound:** short 8-bit blips for sparks, petting, eating, buying and every
  arcade game, and a jingle when Bitsy is fed.
- **Low-energy:** after 20 s without a touch Bitsy dozes and the screen dims;
  at 30 s it turns off. Steps keep counting. Tap the screen or press the AI
  button to wake it.
- **Pocket mode:** on battery, press the AI button (or just let the screen go
  dark) and a few seconds later Bitsy goes into **deep sleep**: screen, Wi-Fi
  and processor off, while the step sensor keeps counting on its own. **Tap
  the screen** to wake it: the board restarts in a few seconds and adds every
  step you walked. If you don't touch it again within 8 seconds it goes back
  to sleep, so a bump in your pocket costs almost nothing. Plugging in USB
  doesn't wake it; tap first.
- **TAP STEPS** (Settings, off by default): an accessibility mode for people
  who can't walk. Every 10 pats on Bitsy count as one step, with no daily cap.
- **Stats web page** on your network (`http://pixelmate.local`): today's
  steps, food, health, a 7-day chart, your streak, sparks, battery and the
  photo gallery.
- **Settings** in the pull-down menu: volume, daily goal (1000 to 30000),
  Wi-Fi, screen sleep, brightness, TAP STEPS, MODULE LIGHTS (the Interaction
  module's mood glow), About and reset.

## Hardware

- ESP-Mosaico (ESP32-S31) with its 480×480 touch screen, BMI270 IMU and speaker.
- Optional: a camera module in the left slot, for the photo booth and webcam.
- Optional: the Interaction module in the right slot, for IR, motion and light.
- Battery recommended: PixelMate shows the charge, dims, and deep-sleeps in your
  pocket to save power.

## Getting started

1. Install PixelMate from Mosaico Ideas and open it.
2. Walk! Sparks arrive as you go (20 steps = 1 spark). Pull down from the top
   edge for the menu: STORE, STYLE and SETTINGS.
3. Swipe left and right between Bitsy, the steps ring, feeding, the arcade,
   TOOLS and (with a camera plugged in) the photo booth.
4. Optional Wi-Fi: PixelMate uses the Wi-Fi saved in Vibe Mode. To set up a
   different network, pull down the menu and tap **SET UP WIFI** (or the
   network line), join the `PIXELMATE-XXXX` hotspot from your phone and pick
   your network. Then open `http://pixelmate.local`.

## Good to know

- Bitsy saves to the board's storage and survives restarts.
- Daily health needs the real date, which comes from Wi-Fi time sync. Clock
  mode needs it too.
- Anyone on your Wi-Fi network can open the stats page and photo gallery; the
  webcam always needs the code shown on the screen.
- Version 1.0.0 is a one-person project, developed on one ESP-Mosaico board
  (with an SC101IOT camera module and the Interaction module) and in the host
  simulator. Checked on the board: step counting with the screen off, saving,
  battery readout, dimming and sleep, pocket deep sleep with tap-to-wake and
  steps counted while asleep, Wi-Fi with the stats page and daily health,
  sounds, the photo booth with hot-plugging, and Interaction module detection.
  Still to measure: real-world battery life in pocket mode. The TOOLS page is
  the newest part and has had the least time on real hardware; the Wi-Fi setup
  hotspot has not been tried from a phone yet, and the OV3640 camera is
  supported but untested. Feedback is welcome.

## Coming next

- **Pan & tilt module**: give Bitsy a neck, so the webcam can look around.
- More mini games and moods.
- Bitsy as a voice assistant, using the board's microphone and AI button.

Follow along at [www.luigipacheco.com](https://www.luigipacheco.com).

## License

PixelMate is free software by Luis Pacheco, released under the
[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0); keep the
NOTICE file with any copy or fork. Some files come from Espressif's
ESP-Mosaico application template and keep their own license header. The
store images use the DejaVu fonts (free license).
