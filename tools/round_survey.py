"""Tour PixelMate's screens in the Host sim and report what a round glass clips.

usage (MSYS2 ucrt64 gcc and Pillow installed):
    python tools/round_survey.py <out_dir> [round]
  round: run the sim with PIXELMATE_ROUND=1 (the round layout).
Writes <name>.png per screen, <name>_clip.png (the circle drawn, clipped
pixels in red) and prints, per screen, the clipped pixel count and boxes.
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

CX, CY, R = 240, 240, 233          # Glass circle in game pixels (1:1, centred).
events, marks = [], {}
frame = 5


def wait(n):
    global frame
    frame += n


def tap(x, y, settle=10):
    events.append({"frame": frame, "type": "tap", "x": x, "y": y})
    wait(settle)


def drag(x0, y0, x1, y1, settle=25):
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
    drag(420, 240, 80, 240, settle=35)


def menu():
    drag(240, 20, 240, 260, settle=30)


def close_panel():
    drag(240, 420, 240, 60, settle=30)


for i in range(40):
    action("jump")                 # some steps
action(9)                          # own everything, +1000 sparks
wait(40)
mark("01_home")
menu()
mark("02_menu")
tap(240, 97, 30)                   # STORE
mark("03_store")
close_panel()
menu()
tap(240, 175, 30)                  # STYLE
mark("04_style")
close_panel()
menu()
tap(240, 253, 30)                  # SETTINGS
mark("05_settings1")
drag(400, 240, 100, 240, settle=25)
mark("06_settings2")
drag(400, 240, 100, 240, settle=25)
mark("07_settings3")
close_panel()
menu()
tap(240, 305, 30)                  # Wi-Fi line
mark("08_wifi")
close_panel()
next_page()
mark("09_steps")
next_page()
mark("10_feed")
next_page()
wait(30)
mark("11_arcade")
# Each game: tap its cabinet (these points hit the square and the round
# cabinets), look, then EXIT (also a point inside both layouts' button).
for name, x in (("14_slots", 100), ("15_stack", 240), ("16_grid", 380)):
    tap(x, 260, 40)
    mark(name)
    tap(110, 100, 30)
drag(240, 400, 240, 200, settle=30)   # Second row of cabinets.
for name, x in (("17_bits", 100), ("18_says", 240), ("19_hilo", 380)):
    tap(x, 260, 40)
    mark(name)
    tap(110, 100, 30)
next_page()
mark("12_tools")
tap(110, 170, 30)                     # First tool (sensors).
mark("20_tool")
tap(110, 100, 30)                     # BACK
next_page()
wait(30)
mark("13_booth")


def clip_report(png, out_png):
    img = Image.open(png).convert("RGB")
    w, h = img.size
    px = img.load()
    hits = []
    for y in range(h):
        for x in range(w):
            if (x - CX) ** 2 + (y - CY) ** 2 > R * R:
                r, g, b = px[x, y]
                if r + g + b > 60:
                    hits.append((x, y))
    over = img.copy()
    d = ImageDraw.Draw(over)
    for x, y in hits:
        over.putpixel((x, y), (255, 0, 0))
    d.ellipse((CX - R, CY - R, CX + R, CY + R), outline=(0, 160, 255), width=2)
    over.save(out_png)
    # Group clipped pixels into coarse 40-px cells for a readable summary.
    cells = {}
    for x, y in hits:
        key = (x // 40, y // 40)
        cells[key] = cells.get(key, 0) + 1
    boxes = sorted(((k[0] * 40, k[1] * 40, n) for k, n in cells.items() if n >= 8), key=lambda t: (t[1], t[0]))
    return len(hits), boxes


def main():
    out = Path(sys.argv[1]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[1]
    workspace = repo.parents[1]   # esp-mosaico-vibe: this repo lives in its projects/
    project = repo.relative_to(workspace).as_posix()
    replay = out / "replay.json"
    replay.write_text(json.dumps({"events": sorted(events, key=lambda e: e["frame"])}))
    env = dict(os.environ)
    env["PATH"] = r"C:\msys64\ucrt64\bin;" + env["PATH"]
    if len(sys.argv) > 2 and sys.argv[2] == "round":
        env["PIXELMATE_ROUND"] = "1"
    for name, at in marks.items():
        state = out / f"{name}.state.json"
        r = subprocess.run([sys.executable, "mosaico.py", "game", "sim", "--project", project, "--headless",
                            "--frames", str(at), "--scenario", str(replay), "--state-output", str(state)],
                           cwd=workspace, env=env, capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"{name} failed: {r.stderr[-1500:]}")
        shutil.copy(workspace / project / "build-host" / "frame.png", out / f"{name}.png")
        s = json.loads(state.read_text())
        n, boxes = clip_report(out / f"{name}.png", out / f"{name}_clip.png")
        where = " ".join(f"({x},{y}):{c}" for x, y, c in boxes)
        print(f"{name:14s} screen={s['screen']:7s} overlay={s['overlay']:9s} clipped={n:5d}  {where}")
    subprocess.run(["git", "-C", str(workspace / project), "checkout", "--", "assets/generated"],
                   capture_output=True)


if __name__ == "__main__":
    main()
