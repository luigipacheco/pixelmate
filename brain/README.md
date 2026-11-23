# Bitsy's brain: teach your pet new acts

Bitsy's behaviours ("acts") are data, not firmware. This folder holds the
program that runs on your computer and sends acts to Bitsy over Wi-Fi: no
cable, no reflash. How the whole system works is in
[docs/LEARNING.md](../docs/LEARNING.md).

- `bitsy_brain.py`: the program (Python 3, nothing to install).
- `acts/`: example acts to copy from: `wow.json`, `drowsy.json`, `lunch.json`.

**You need:** PixelMate firmware with learning (the `feat/learning` branch for
now), and Bitsy awake on the same Wi-Fi as your computer. On USB power it stays
reachable even while it shows the clock. In pocket deep sleep it is not: tap it
awake first.

## Quick start

Run these from the project folder. Use your pet's address: it is on the Wi-Fi
panel of the pull-down menu, or try `pixelmate.local`.

```
python brain/bitsy_brain.py --host 192.168.1.142 show
python brain/bitsy_brain.py --host 192.168.1.142 add brain/acts/wow.json
```

Now pat Bitsy three times. To skip `--host` every time, set it once:
`set BITSY_HOST=192.168.1.142` (cmd), `$env:BITSY_HOST = "192.168.1.142"`
(PowerShell) or `export BITSY_HOST=192.168.1.142` (macOS, Linux).

## Make a new act

1. **Copy an example**, for instance `acts/drowsy.json` to `acts/dance.json`.
2. **Give it its own `id` and `name`.** The id is any number from 1 to 65535
   that no other act uses (`show` lists the ones taken; 1 to 5 are built in).
   The name is up to 11 characters: A-Z, 0-9, `_` and `-`.
3. **Pick when it plays** with `on` (see *Triggers*).
4. **Write the steps**, up to 8. Each one is a pose that holds for `ticks`
   (30 ticks = 1 second, at most 90 per step).
5. **Send it:** `python brain/bitsy_brain.py add brain/acts/dance.json`
6. **Watch it** on the home screen. If a value is out of range, Bitsy refuses
   the act, says which value, and keeps what it had.
7. **Adjust and send again.** `add` replaces the act with the same id or name.

Bitsy holds 12 acts. Free a place with `remove NAME`.

### An act, field by field

```json
{
  "id": 7,
  "name": "DROWSY",
  "say": "YAWN",
  "on": "idle",
  "param": 14,
  "cooldown": 3,
  "chance": 100,
  "steps": [
    {"mood": "sleepy", "eyes": "round", "color": "pet", "look": [0, 8],
     "hop": 0, "fx": "zzz", "sound": "none", "ticks": 26}
  ]
}
```

| Field | Meaning |
|---|---|
| `say` | Optional text that floats up when the act starts. Up to 13 characters: A-Z, 0-9, space and `! ? + - .` |
| `on`, `param`, `param2` | When it plays: see *Triggers*. |
| `cooldown` | Minutes before it may play again (0 to 10080). |
| `chance` | 0 to 100: how often a trigger really plays it. `learn` tunes this number. |

### Triggers

| `on` | Plays | `param` |
|---|---|---|
| `pat` | every Nth pat | N, 1 to 1000 |
| `steps` | each time today's steps pass a multiple of N | N, 10 to 50000 |
| `goal` | when the daily goal is reached | |
| `fed` | when today's food is walked | |
| `wake` | when Bitsy wakes or starts | |
| `idle` | after N seconds alone on the home screen | N, 3 to 600 |
| `time` | between two times of day, `"param": "12:30", "param2": "13:30"` | needs `cooldown` of 60 or more |
| `random` | about once every N minutes | N, 1 to 720 |
| `never` | switched off | |

### Steps

| Field | Values |
|---|---|
| `mood` | `idle` `happy` `sleepy` `hungry` `sad` `walk` `eating` `thinking` `surprised` `grumpy`, or `keep` for whatever Bitsy feels |
| `eyes` | `round` `stars` `hearts` `spirals` |
| `color` | `pet` (its own color), `rainbow`, or a palette number 0 to 5 |
| `look` | `[x, y]`, each -40 to 40: where the eyes look (`[-30, 0]` is left, `[0, 14]` is down) |
| `hop` | 0 to 12: a jump when the step starts |
| `fx` | `none` `hearts` `sparks` `bolts` `zzz` `shake`, when the step starts |
| `sound` | `none` `pet` `spark` `tap` `wake` `win` `coins` `zap` `buzz` `fed` `goal`, when the step starts |
| `ticks` | 1 to 90: how long the step lasts (30 = 1 s) |

A step you leave out a field of gets `keep`, `round`, `pet`, `[0, 0]`, 0,
`none`, `none` and 15 ticks.

### Good to know

- **Acts play on the home screen only**, one at a time. A touch cuts the act
  short, and so does leaving the page.
- **`idle` acts must fit before the screen dims.** The screen dims after two
  thirds of the SCREEN SLEEP setting (20 s of the default 30 s), and a dimmed
  pet does not play acts. With the default, start by about 14 s and keep the
  act to 3 or 4 seconds. BORED (built in) plays at 12 s some of the time, which
  pushes later idle acts back by its 2.6 s.
- **Short is better.** One to three seconds reads as a reaction; longer feels
  like a cutscene.
- **A language model can write acts.** Give it this README and an example, ask
  for an act in this format, and `add` the result.

## Manage what Bitsy knows

```
python brain/bitsy_brain.py show               # list the acts
python brain/bitsy_brain.py add FILE           # teach one act (or a file of several)
python brain/bitsy_brain.py remove NAME        # forget one, by name or id
python brain/bitsy_brain.py export pack.json   # save all of them to a file
python brain/bitsy_brain.py push pack.json     # replace all of them with a file
python brain/bitsy_brain.py defaults           # back to the five built-in acts
```

Acts are saved on the board and survive restarts.

## The diary and learning

```
python brain/bitsy_brain.py sync               # new diary entries -> bitsy_diary.jsonl
python brain/bitsy_brain.py learn              # what it would change
python brain/bitsy_brain.py learn --apply      # change it
```

`sync` copies what happened to Bitsy (walks, pats, meals, games, and each act
it played and how it ended) into `bitsy_diary.jsonl`, in the folder you run it
from. Bitsy keeps the last 512 entries, so sync at least every day or two.

`learn` reads that file: acts you answer with pats play more often, acts you
never answer and usually cut short play less (never below 20 %). An act needs
5 plays before `learn` touches it.

## If it does not work

- **"can't reach ..."**: Bitsy is asleep or off Wi-Fi, or the address is wrong.
  Wake it (tap the screen), keep it on USB, and check the address on its Wi-Fi
  panel.
- **"400 ..."**: Bitsy refused the act; the message names the act, the step and
  the allowed range.
- **The act never plays:** check `show` for its trigger, `chance` and
  `cooldown`; make sure Bitsy is on the home screen and awake (not dimmed).

Anyone on your Wi-Fi can send acts: these addresses have no password, like the
stats page. An act can only change Bitsy's face, sounds and timing.
