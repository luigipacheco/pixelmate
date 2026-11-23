#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bitsy's brain: a small program for your computer that reads the pet's diary
and teaches it behaviours ("acts") over the local network. How to write acts:
brain/README.md. How it all fits together: docs/LEARNING.md.

    python brain/bitsy_brain.py show                  # the acts Bitsy has now
    python brain/bitsy_brain.py add brain/acts/wow.json   # teach one more act
    python brain/bitsy_brain.py remove WOW            # forget one
    python brain/bitsy_brain.py export pack.json      # save all of them as an editable file
    python brain/bitsy_brain.py push pack.json        # replace all of them with a file
    python brain/bitsy_brain.py defaults              # back to the built-in acts
    python brain/bitsy_brain.py sync                  # copy new diary entries to bitsy_diary.jsonl
    python brain/bitsy_brain.py learn                 # suggest changes from the diary (--apply sends them)

The pet's address comes from --host, or the BITSY_HOST environment variable, or
pixelmate.local. Only the Python standard library is needed.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

TRIGGERS = ["never", "pat", "steps", "goal", "fed", "wake", "idle", "time", "random"]
MOODS = ["idle", "happy", "sleepy", "hungry", "sad", "walk", "eating", "thinking", "surprised", "grumpy"]
MOOD_KEEP = 255
EYES = ["round", "stars", "hearts", "spirals"]
FX = ["none", "hearts", "sparks", "bolts", "zzz", "shake"]
SOUNDS = ["none", "pet", "spark", "tap", "wake", "win", "coins", "zap", "buzz", "fed", "goal"]
ENDS = ["finished", "cut by a touch", "cut short"]
COLOR_FIRST = 2
MAX_ACTS, MAX_STEPS = 12, 8


class BrainError(Exception):
    pass


# ---- Talking to the pet ----------------------------------------------------

def base_url(host):
    host = host or os.environ.get("BITSY_HOST") or "pixelmate.local"
    return host if host.startswith("http") else f"http://{host}"


def request(host, path, body=None):
    url = base_url(host) + path
    data = body.encode("ascii") if body is not None else None
    req = urllib.request.Request(url, data=data, method="POST" if data is not None else "GET")
    try:
        with urllib.request.urlopen(req, timeout=15) as reply:
            return reply.read().decode("utf-8")
    except urllib.error.HTTPError as err:
        raise BrainError(f"{url}: {err.code} {err.read().decode('utf-8', 'replace').strip()}") from err
    except (urllib.error.URLError, OSError) as err:
        raise BrainError(f"can't reach {url} ({err}). Is Bitsy awake, on Wi-Fi and on USB? "
                         f"Try --host with its IP address.") from err


# ---- Pack formats ----------------------------------------------------------
# The pet speaks numbers; people (and language models) get names.

def index(names, value, what):
    if isinstance(value, int):
        if 0 <= value < len(names):
            return value
    elif isinstance(value, str) and value.lower() in names:
        return names.index(value.lower())
    raise BrainError(f"{what}: {value!r} is not one of {', '.join(names)}")


def minutes(value, what):
    if isinstance(value, str) and ":" in value:
        hours, mins = value.split(":", 1)
        return int(hours) * 60 + int(mins)
    if isinstance(value, int):
        return value
    raise BrainError(f"{what}: {value!r} should be a number or HH:MM")


def clock(value):
    return f"{value // 60:02d}:{value % 60:02d}"


def step_to_numbers(step, where):
    mood = step.get("mood", "keep")
    mood = MOOD_KEEP if mood in ("keep", MOOD_KEEP) else index(MOODS, mood, f"{where} mood")
    color = step.get("color", "pet")
    if color == "pet":
        color = 0
    elif color == "rainbow":
        color = 1
    elif isinstance(color, int):
        color += COLOR_FIRST          # A palette index, 0..5.
    else:
        raise BrainError(f"{where} color: {color!r} should be pet, rainbow or a palette number 0..5")
    look = step.get("look", [0, 0])
    return [mood, index(EYES, step.get("eyes", "round"), f"{where} eyes"), color, int(look[0]), int(look[1]),
            int(step.get("hop", 0)), index(FX, step.get("fx", "none"), f"{where} fx"),
            index(SOUNDS, step.get("sound", "none"), f"{where} sound"), int(step.get("ticks", 15))]


def pack_to_wire(pack):
    """Friendly JSON (a dict) -> the line format the pet accepts."""
    acts = pack.get("acts", [])
    if not 1 <= len(acts) <= MAX_ACTS:
        raise BrainError(f"a pack holds 1..{MAX_ACTS} acts")
    lines = []
    for act in acts:
        name = str(act["name"]).upper()
        trigger = index(TRIGGERS, act.get("on", "never"), f"{name} on")
        timed = TRIGGERS[trigger] == "time"
        param = minutes(act.get("param", 0), f"{name} param") if timed else int(act.get("param", 0))
        param2 = minutes(act.get("param2", 0), f"{name} param2") if timed else int(act.get("param2", 0))
        lines.append(f"act {int(act['id'])} {name} {trigger} {param} {param2} "
                     f"{int(act.get('cooldown', 0))} {int(act.get('chance', 100))}")
        if act.get("say"):
            lines.append(f"say {str(act['say']).upper()}")
        steps = act.get("steps", [])
        if not 1 <= len(steps) <= MAX_STEPS:
            raise BrainError(f"{name}: needs 1..{MAX_STEPS} steps")
        for n, step in enumerate(steps, 1):
            lines.append("step " + " ".join(str(v) for v in step_to_numbers(step, f"{name} step {n}")))
    return "\n".join(lines) + "\n"


def pack_from_pet(raw):
    """The pet's JSON (numbers) -> friendly JSON."""
    acts = []
    for act in raw["acts"]:
        timed = TRIGGERS[act["trigger"]] == "time"
        steps = []
        for mood, eyes, color, look_x, look_y, hop, fx, sound, ticks in act["steps"]:
            steps.append({
                "mood": "keep" if mood == MOOD_KEEP else MOODS[mood], "eyes": EYES[eyes],
                "color": "pet" if color == 0 else "rainbow" if color == 1 else color - COLOR_FIRST,
                "look": [look_x, look_y], "hop": hop, "fx": FX[fx], "sound": SOUNDS[sound], "ticks": ticks,
            })
        acts.append({
            "id": act["id"], "name": act["name"], "say": act["say"], "on": TRIGGERS[act["trigger"]],
            "param": clock(act["param"]) if timed else act["param"],
            "param2": clock(act["param2"]) if timed else act["param2"],
            "cooldown": act["cooldown"], "chance": act["chance"], "steps": steps,
        })
    return {"acts": acts}


def fetch_pack(host):
    return pack_from_pet(json.loads(request(host, "/api/behaviors")))


# ---- Commands --------------------------------------------------------------

def describe_trigger(act):
    on, param, param2 = act["on"], act["param"], act["param2"]
    return {
        "pat": f"every {param} pats", "steps": f"every {param} steps", "goal": "daily goal reached",
        "fed": "today's food walked", "wake": "on waking", "idle": f"after {param} s left alone",
        "time": f"between {param} and {param2}", "random": f"about every {param} min",
    }.get(on, "never")


def cmd_show(args):
    pack = fetch_pack(args.host)
    for act in pack["acts"]:
        seconds = sum(s["ticks"] for s in act["steps"]) / 30
        say = f' says "{act["say"]}"' if act["say"] else ""
        print(f"{act['id']:5d} {act['name']:<11} {describe_trigger(act):<28} chance {act['chance']:3d}%  "
              f"cooldown {act['cooldown']} min  {len(act['steps'])} steps, {seconds:.1f} s{say}")


def cmd_export(args):
    Path(args.file).write_text(json.dumps(fetch_pack(args.host), indent=2) + "\n", encoding="utf-8")
    print(f"saved {args.file}")


def cmd_push(args):
    pack = json.loads(Path(args.file).read_text(encoding="utf-8"))
    print(request(args.host, "/api/behaviors", pack_to_wire(pack)).strip())


def cmd_defaults(args):
    print(request(args.host, "/api/behaviors", "defaults\n").strip())


def load_acts(path):
    """A file holds one act, or a pack: {"acts": [...]}."""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    return data["acts"] if isinstance(data, dict) and "acts" in data else [data]


def cmd_add(args):
    """Teach Bitsy the act(s) in a file, keeping what it already knows."""
    pack = fetch_pack(args.host)
    for act in load_acts(args.file):
        name = str(act["name"]).upper()
        replaced = [a["name"] for a in pack["acts"] if a["id"] == act["id"] or a["name"] == name]
        pack["acts"] = [a for a in pack["acts"] if a["id"] != act["id"] and a["name"] != name]
        pack["acts"].append(act)
        print(f"{name}: {'replaces ' + ', '.join(replaced) if replaced else 'new'}")
    if len(pack["acts"]) > MAX_ACTS:
        raise BrainError(f"Bitsy holds {MAX_ACTS} acts; remove one first (it would have {len(pack['acts'])})")
    print(request(args.host, "/api/behaviors", pack_to_wire(pack)).strip())


def cmd_remove(args):
    """Make Bitsy forget an act, by name or id."""
    pack = fetch_pack(args.host)
    key = args.act.upper()
    keep = [a for a in pack["acts"] if a["name"] != key and str(a["id"]) != key]
    if len(keep) == len(pack["acts"]):
        raise BrainError(f"no act called {args.act!r}; see 'show'")
    if not keep:
        raise BrainError("that is the last act; use 'defaults' to start over")
    pack["acts"] = keep
    print(request(args.host, "/api/behaviors", pack_to_wire(pack)).strip())


def read_state(path):
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else {"next": 0}


def cmd_sync(args):
    out = Path(args.out)
    state_path = out.with_suffix(".state.json")
    state = read_state(state_path)
    reply = json.loads(request(args.host, f"/api/diary?since={state['next']}"))
    events = reply["events"]
    if events and events[0]["n"] > state["next"] and state["next"]:
        print(f"note: {events[0]['n'] - state['next']} entries were overwritten before this sync "
              f"(the pet keeps the last 512)")
    with out.open("a", encoding="utf-8") as f:
        for event in events:
            f.write(json.dumps(event) + "\n")
    state["next"] = reply["next"]
    state_path.write_text(json.dumps(state), encoding="utf-8")
    print(f"{len(events)} new entries -> {out} (next: {reply['next']})")
    for event in events[-args.tail:]:
        when = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(event["t"])) if event["t"] else "(clock not set)"
        print(f"  {when}  {event['e']:<7} a={event['a']} v={event['v']}")


def act_scores(diary_path):
    """Per act id: plays, finished, cut by a touch, pats during or right after it."""
    scores = {}
    for line in Path(diary_path).read_text(encoding="utf-8").splitlines():
        event = json.loads(line)
        if event["e"] == "act":
            s = scores.setdefault(event["v"], {"plays": 0, "finished": 0, "touch": 0, "pats": 0})
            s["plays"] += 1
            s["finished"] += event["a"] == 0
            s["touch"] += event["a"] == 1
        elif event["e"] == "pat" and event["a"] == 1:
            scores.setdefault(event["v"], {"plays": 0, "finished": 0, "touch": 0, "pats": 0})["pats"] += 1
    return scores


def cmd_learn(args):
    """Preference learning: acts that get patted play more, ignored ones less."""
    if not Path(args.diary).exists():
        raise BrainError(f"{args.diary} not found: run sync first")
    scores = act_scores(args.diary)
    pack = fetch_pack(args.host)
    changed = False
    for act in pack["acts"]:
        s = scores.get(act["id"])
        if not s or s["plays"] < args.min_plays:
            print(f"{act['name']:<11} not enough plays yet ({s['plays'] if s else 0}/{args.min_plays})")
            continue
        liked = s["pats"] / s["plays"]              # Pats per play: you answered it.
        watched = s["finished"] / s["plays"]        # You let it finish.
        if liked >= 0.3:
            step = 10
        elif liked == 0 and watched < 0.5:
            step = -10
        else:
            step = 0
        new = max(args.floor, min(100, act["chance"] + step))
        print(f"{act['name']:<11} plays {s['plays']:3d}  pats/play {liked:.2f}  finished {watched:.0%}  "
              f"chance {act['chance']}% -> {new}%")
        changed |= new != act["chance"]
        act["chance"] = new
    if not changed:
        print("nothing to change")
    elif args.apply:
        print(request(args.host, "/api/behaviors", pack_to_wire(pack)).strip())
    else:
        print("(dry run: add --apply to send these changes to Bitsy)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", help="the pet's address (default: BITSY_HOST or pixelmate.local)")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("sync", help="copy new diary entries to a file")
    p.add_argument("--out", default="bitsy_diary.jsonl")
    p.add_argument("--tail", type=int, default=8, help="how many new entries to print")
    p.set_defaults(run=cmd_sync)
    sub.add_parser("show", help="list the behaviours in use").set_defaults(run=cmd_show)
    p = sub.add_parser("export", help="save the behaviours as an editable JSON file")
    p.add_argument("file")
    p.set_defaults(run=cmd_export)
    p = sub.add_parser("push", help="send the behaviours in a JSON file")
    p.add_argument("file")
    p.set_defaults(run=cmd_push)
    p = sub.add_parser("add", help="teach the act(s) in a JSON file, keeping the rest")
    p.add_argument("file")
    p.set_defaults(run=cmd_add)
    p = sub.add_parser("remove", help="forget an act, by name or id")
    p.add_argument("act")
    p.set_defaults(run=cmd_remove)
    sub.add_parser("defaults", help="restore the built-in behaviours").set_defaults(run=cmd_defaults)
    p = sub.add_parser("learn", help="tune how often each behaviour plays, from the diary")
    p.add_argument("--diary", default="bitsy_diary.jsonl")
    p.add_argument("--min-plays", type=int, default=5)
    p.add_argument("--floor", type=int, default=20, help="never go below this chance")
    p.add_argument("--apply", action="store_true")
    p.set_defaults(run=cmd_learn)
    args = parser.parse_args()
    try:
        args.run(args)
    except BrainError as err:
        sys.exit(f"bitsy_brain: {err}")


if __name__ == "__main__":
    main()
