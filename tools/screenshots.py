"""Capture PixelMate store/README screenshots with the Host simulator.

usage (from the workspace root, MSYS2 ucrt64 gcc on PATH):
    python projects/pixel_pal/tools/screenshots.py projects/pixel_pal/ideas/screens [names...]

Replays one scripted session (walk, unlock everything with the host-only
action 9, visit each screen) and saves the frame at each checkpoint. The
project is the folder this script lives in, so it also works from a git
worktree. Host-only replay actions: 9 owns everything (+1000 sparks),
8 pretends USB power (clock mode once the screen sleeps).
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

events, marks = [], {}
frame = 5


def wait(n):
    global frame
    frame += n


def tap(x, y, settle=8):
    events.append({"frame": frame, "type": "tap", "x": x, "y": y})
    wait(settle)


def drag(x0, y0, x1, y1, settle=20):
    for i in range(5):
        events.append({"frame": frame + i, "type": "pointer", "x": x0 + (x1 - x0) * i // 4,
                       "y": y0 + (y1 - y0) * i // 4, "pressed": i < 4})
    wait(5 + settle)


def action(code):
    events.append({"frame": frame, "type": "action", "code": code, "pressed": True})
    events.append({"frame": frame + 1, "type": "action", "code": code, "pressed": False})
    wait(2)


def mark(name):
    marks[name] = frame


def next_page():
    drag(420, 240, 80, 240, settle=30)


for i in range(124):              # 6200 steps today
    action("jump")
    if i % 60 == 59:
        tap(240, 440, 2)          # keep the screen awake
action(9)                         # host-only: own everything, +1000 sparks
wait(40)
mark("01_home")
next_page()
mark("02_steps")
next_page()                       # feed
next_page()
wait(30)
mark("03_arcade")                 # row 1: SLOTS, STACK, GRID
tap(392, 290, 20)                 # SPARK GRID cabinet (row 1, third)
tap(426, 158, 30)                 # PLAY
tap(78, 152, 20)                  # flip three tiles (this replay's board: row 4 has no duds)
tap(78, 308, 20)
tap(234, 308, 30)
mark("04_grid")
tap(64, 94, 20)                   # EXIT
drag(240, 400, 240, 200, settle=30)   # row 2: BIT FLIP, BITSY SAYS, HI-LO
tap(240, 290, 20)                 # BITSY SAYS (row 2, middle)
tap(240, 302, 30)                 # PLAY
mark("05_says")                   # the red pad is lit and the eyes turn red
wait(200)
tap(64, 94, 20)                   # EXIT
next_page()                       # TOOLS
wait(30)
mark("06_tools")
next_page()                       # photo booth (host camera is plugged in)
wait(30)
tap(54, 145, 30)                  # film chip -> COLOR
mark("07_booth_color")
drag(240, 30, 240, 230)           # menu
tap(240, 253, 20)                 # SETTINGS
drag(400, 240, 100, 240)
tap(240, 276, 40)                 # ABOUT (page 2: BRIGHTNESS, TAP STEPS, ABOUT, RESET)
mark("09_about")
drag(240, 400, 240, 100, settle=20)
action(8)                         # host-only: pretend USB power
wait(2400)                        # doze, screen off, clock
mark("08_clock")


def main():
    out = Path(sys.argv[1]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    project_dir = Path(__file__).resolve().parents[1]
    workspace = project_dir.parents[1]
    project = project_dir.relative_to(workspace).as_posix()
    replay = out / "replay.json"
    replay.write_text(json.dumps({"events": sorted(events, key=lambda e: e["frame"])}))
    env = dict(os.environ)
    env["PATH"] = r"C:\msys64\ucrt64\bin;" + env["PATH"]
    wanted = sys.argv[2:] or list(marks)
    for name in wanted:
        state = out / f"{name}.state.json"
        r = subprocess.run([sys.executable, "mosaico.py", "game", "sim", "--project", project, "--headless",
                            "--frames", str(marks[name]), "--scenario", str(replay), "--state-output", str(state)],
                           cwd=workspace, env=env, capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"{name} failed: {r.stderr[-1500:]}")
        shutil.copy(project_dir / "build-host" / "frame.png", out / f"{name}.png")
        s = json.loads(state.read_text())
        state.unlink()
        print(f"{name:16s} frame={marks[name]} screen={s['screen']} overlay={s['overlay']} "
              f"arcade={s.get('arcade')} power={s.get('power')}")
    replay.unlink()


if __name__ == "__main__":
    main()
