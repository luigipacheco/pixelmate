# How Bitsy learns

Bitsy's body is the ESP-Mosaico. Its instincts live there and work with no
network at all. A stronger computer on the same Wi-Fi can act as a **brain**
that Bitsy visits now and then: it reads what happened to the pet and sends
back new or retuned behaviours. Nothing leaves your network.

```
  ESP-Mosaico (body)                        your computer (brain)
  ------------------                        ---------------------
  diary: what happened   --- GET /api/diary --->   bitsy_diary.jsonl
  acts: behaviours as data <-- POST /api/behaviors --   learn / invent
```

## The two pieces on the board

### 1. The diary

A ring of the last 512 small events, each with a time stamp:

| Event | Meaning | `a` | `v` |
|---|---|---|---|
| `boot` | The pet started | 1 = woke from pocket sleep | |
| `walk` | Steps in the last 5 minutes | 1 = counted while asleep | steps |
| `pat` | Pats | 0 = plain pats in the last minute; 1 = a pat during or within 10 s of an act | count, or the act's id |
| `act` | An act ended | 0 finished, 1 cut by a touch, 2 cut short otherwise | act id |
| `feed` | A snack | snack | |
| `game` | An arcade result | cabinet | sparks paid |
| `screen` | A page was opened | page | |
| `power` | Awake (0) or asleep (2) | state | |
| `goal`, `fed`, `day`, `buy`, `revive` | Daily milestones | | steps / 10 where it applies |

`GET /api/diary?since=N` returns `{"events":[{"n":..,"t":..,"e":"pat","a":1,"v":4},...],"next":M}`.
Ask again with `since=M` to get only what is new. `t` is seconds since 1970,
or 0 while the clock is not set.

The diary lives in memory that survives deep sleep and restarts, and is copied
to flash every 30 minutes (and before a pocket sleep), so a flat battery loses
little. Code: `components/app_game_runtime/pet_diary.c`.

### 2. Acts: behaviours as data

An **act** is a trigger plus a script of up to 8 steps. Each step sets an
expression, eye shape, eye color, where the eyes look, a hop, particles, a
sound and how long it lasts. The pet has up to 12 acts. The built-in pack:

| id | Name | Plays |
|---|---|---|
| 1 | HELLO | When Bitsy wakes or starts |
| 2 | MORNING | Once between 6:00 and 10:00 |
| 3 | BORED | After 12 s left alone on the home screen (60 % of the time) |
| 4 | PURR | Every fifth pat |
| 5 | PEEK | About every 3 minutes |

Acts play on the home screen only, one at a time, and a touch cuts one short.
The hard-coded behaviours from v1 (the dance, star eyes, easter eggs) are
unchanged; acts are added on top.

`GET /api/behaviors` returns the pack as JSON (numbers).
`POST /api/behaviors` replaces it. The body is this line format:

```
act <id> <NAME> <trigger> <param> <param2> <cooldown_min> <chance>
say <TEXT>
step <mood> <eyes> <color> <look_x> <look_y> <hop> <fx> <sound> <ticks>
```

or the single word `defaults` to restore the built-in pack. The numbers:

- **trigger:** 0 never, 1 every `param`-th pat, 2 every `param` steps, 3 goal
  reached, 4 fed, 5 wake, 6 idle `param` seconds, 7 local time `param`..`param2`
  (minutes of the day; needs a cooldown of 60+), 8 about every `param` minutes.
- **mood:** 0 idle, 1 happy, 2 sleepy, 3 hungry, 4 sad, 5 walk, 6 eating,
  7 thinking, 8 surprised, 9 grumpy, 255 keep the pet's own.
- **eyes:** 0 round, 1 stars, 2 hearts, 3 spirals.
- **color:** 0 the pet's color, 1 rainbow, 2..7 palette colors.
- **look_x, look_y:** -40..40. **hop:** 0..12. **ticks:** 1..90 (30 = 1 s).
- **fx:** 0 none, 1 hearts, 2 sparks, 3 bolts, 4 zzz, 5 shake.
- **sound:** 0 none, 1 pet, 2 spark, 3 tap, 4 wake, 5 win, 6 coins, 7 zap,
  8 buzz, 9 fed, 10 goal.
- **chance:** 0..100, how often a trigger really plays the act. This is the
  number preference learning tunes.

Every field is range-checked on the board (`app_act_valid` in
`components/app_game/acts.c`). A bad pack is refused with a reason and the old
one stays. A good one is saved to flash and survives restarts.

**Open on your network:** like the stats page, these routes have no password.
A pack can only make Bitsy pull faces, but anyone on your Wi-Fi can send one.
A pairing code is a possible later step.

## The brain: `tools/bitsy_brain.py`

Standard-library Python, run on any computer on the same network:

```
python tools/bitsy_brain.py --host 192.168.1.142 sync      # diary -> bitsy_diary.jsonl
python tools/bitsy_brain.py --host 192.168.1.142 show      # what Bitsy knows
python tools/bitsy_brain.py --host 192.168.1.142 export pack.json
python tools/bitsy_brain.py --host 192.168.1.142 push pack.json
python tools/bitsy_brain.py --host 192.168.1.142 learn     # add --apply to send
```

`export` writes a friendly file with names instead of numbers, for example:

```json
{"id": 6, "name": "LUNCH", "say": "LUNCH?", "on": "time", "param": "12:30", "param2": "13:30",
 "cooldown": 720, "chance": 100,
 "steps": [{"mood": "hungry", "eyes": "round", "color": "pet", "look": [0, 10], "hop": 0,
            "fx": "none", "sound": "none", "ticks": 30},
           {"mood": "happy", "eyes": "stars", "color": "rainbow", "look": [0, 0], "hop": 6,
            "fx": "sparks", "sound": "coins", "ticks": 40}]}
```

Edit it (or have a language model write new acts in this format) and `push`.

`learn` is the first, simple kind of learning: for each act it counts plays,
how often you let it finish, and pats during or right after it. Acts you
answer with pats get a higher chance; acts you never answer and usually cut
short get a lower one (never below 20 %).

Bitsy must be awake and on Wi-Fi to be reached. The easy moment is on the
charger: on USB it stays on Wi-Fi even while it shows the clock.

## What comes next

1. **Nightly sync:** run `sync` and `learn --apply` on a schedule while Bitsy
   charges ("dreaming").
2. **Invented behaviours:** give a language model the diary and the current
   pack, and let it propose new acts in the friendly format, with an approval
   step on the stats page before they are kept.
3. **More stage:** acts in the arcade and on the clock, and more primitives
   (new eye shapes, short melodies, vibration).
4. **Perception:** small models trained on the computer (your voice, your
   face) that run on the board.
