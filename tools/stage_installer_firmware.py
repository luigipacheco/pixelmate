"""Copy one board's firmware build into the web installer (installer/firmware/<board>/).

usage (from the repo root, after an ESP-IDF build of that board):
    python tools/stage_installer_firmware.py mosaico   build
    python tools/stage_installer_firmware.py stackchan boards/stackchan/build
    python tools/stage_installer_firmware.py stopwatch boards/stopwatch/build

Reads <build>/flasher_args.json and copies only the images that installer/boards.json
lists for the board, at the offsets it lists (on the ESP-Mosaico: partition table and
app slot, never the bootloader or the system partitions). Writes firmware.json with each
file's offset, size and SHA-256; the page checks those before it writes anything.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
INSTALLER = REPO / "installer"


def git(*args):
    try:
        return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def main():
    boards = {b["id"]: b for b in json.loads((INSTALLER / "boards.json").read_text())["boards"]}
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("board", choices=sorted(boards))
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--version", help="shown on the page (default: git describe)")
    args = parser.parse_args()

    board = boards[args.board]
    build = args.build_dir.resolve()
    flasher = json.loads((build / "flasher_args.json").read_text())
    chip = flasher.get("extra_esptool_args", {}).get("chip", "")
    if chip.replace("-", "").lower() != board["chip"].replace("-", "").lower():
        raise SystemExit(f"{build} was built for {chip or 'an unknown chip'}, "
                         f"but {board['name']} is {board['chip']}")
    built = {int(offset, 16): build / path for offset, path in flasher["flash_files"].items()}

    out = INSTALLER / "firmware" / board["id"]
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    files = []
    for want in board["files"]:
        offset = int(want["offset"], 16)
        src = built.get(offset)
        if src is None or not src.is_file():
            raise SystemExit(f"the build has no image at {want['offset']} ({want['role']})")
        data = src.read_bytes()
        name = f"{want['role']}.bin"
        (out / name).write_bytes(data)
        files.append({"role": want["role"], "offset": want["offset"], "file": name,
                      "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        print(f"  {want['offset']:>9}  {name:<22} {len(data):>9} bytes  (from {src.relative_to(build)})")
    for offset in sorted(set(built) - {int(f["offset"], 16) for f in board["files"]}):
        print(f"  {hex(offset):>9}  left out: {built[offset].relative_to(build)}")

    commit = git("rev-parse", "--short", "HEAD")
    manifest = {
        "board": board["id"],
        "chip": board["chip"],
        "version": args.version or git("describe", "--tags", "--always", "--dirty") or "dev",
        "commit": commit,
        "built": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "flash": flasher.get("flash_settings", {}),
        "files": files,
    }
    (out / "firmware.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Staged {board['name']} {manifest['version']} in {out.relative_to(REPO)}")


if __name__ == "__main__":
    main()
