"""Capture PixelMate store/README screenshots with the Host simulator.

usage (from the workspace root, MSYS2 ucrt64 gcc on PATH):
    python projects/pixel_pal/tools/screenshots.py projects/pixel_pal/ideas/screens

Replays one scripted session (walk, unlock everything with the host-only
action 9, visit each screen) and saves the frame at each checkpoint.
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
next_page()
mark("03_feed")
next_page()
wait(30)
mark("04_arcade")
drag(240, 400, 240, 200, settle=30)   # row 2: BIT FLIP, BITSY SAYS, HI-LO
tap(88, 290, 20)                  # BIT FLIP cabinet (row 2, first)
tap(240, 412, 30)                 # PLAY
tap(16 + 4 * 56 + 26, 308, 6)     # flip two switches
tap(16 + 6 * 56 + 26, 308, 45)
mark("05_bitflip")
tap(64, 94, 20)                   # EXIT (lobby stays on row 2)
tap(392, 290, 20)                 # HI-LO (row 2, third)
tap(240, 394, 40)                 # DEAL
mark("06_hilo")
tap(64, 94, 20)                   # EXIT
next_page()                       # photo booth (host camera is plugged in)
wait(30)
tap(54, 145, 20)                  # film chip -> COLOR
mark("07_booth_color")
tap(422, 145, 30)                 # ON AIR
mark("08_webcam")
tap(422, 145, 20)                 # off again
drag(240, 30, 240, 230)           # menu
tap(240, 253, 20)                 # SETTINGS
drag(400, 240, 100, 240)
tap(240, 202, 40)                 # ABOUT
mark("09_about")
drag(240, 400, 240, 100, settle=20)


def main():
    out = Path(sys.argv[1]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    workspace = Path(__file__).resolve().parents[3]
    project = "projects/pixel_pal"
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
        shutil.copy(workspace / project / "build-host" / "frame.png", out / f"{name}.png")
        s = json.loads(state.read_text())
        state.unlink()
        print(f"{name:16s} screen={s['screen']} overlay={s['overlay']} arcade={s.get('arcade')}")
    replay.unlink()


if __name__ == "__main__":
    main()
