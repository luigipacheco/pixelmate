"""Build ideas/cover.png (1920x1080) for Mosaico Ideas from the screenshots.

usage (any folder; needs Pillow):
    python projects/pixel_pal/tools/make_cover.py

Run tools/screenshots.py first. Text uses the DejaVu fonts in ideas/fonts.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

IDEAS = Path(__file__).resolve().parents[1] / "ideas"
W, H = 1920, 1080
BG, DOT, FRAME = (10, 12, 8), (22, 26, 16), (40, 46, 30)
YELLOW, ORANGE, TEXT, GREY = (220, 255, 40), (255, 90, 30), (230, 235, 220), (120, 130, 90)
SHOTS = ["01_home", "03_arcade", "07_booth_color", "06_tools"]   # 2x2, reading order
TILE, BORDER, GAP = 400, 10, 20
LEFT = 115


def font(bold, size):
    return ImageFont.truetype(str(IDEAS / "fonts" / ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")), size)


def main():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    for y in range(0, H, 24):
        for x in range(0, W, 24):
            d.rectangle((x, y, x + 2, y + 2), fill=DOT)

    # Screens: a 2x2 grid on the right.
    cell = TILE + 2 * BORDER
    x0 = W - 60 - (2 * cell + GAP)
    y0 = (H - (2 * cell + GAP)) // 2
    for i, name in enumerate(SHOTS):
        x = x0 + (i % 2) * (cell + GAP)
        y = y0 + (i // 2) * (cell + GAP)
        d.rectangle((x, y, x + cell - 1, y + cell - 1), fill=FRAME)
        shot = Image.open(IDEAS / "screens" / f"{name}.png").convert("RGB").resize((TILE, TILE), Image.LANCZOS)
        im.paste(shot, (x + BORDER, y + BORDER))

    # Text on the left, sized to stay clear of the screens.
    room = x0 - LEFT - 50
    size = 150
    while d.textlength("PIXELMATE", font=font(True, size)) > room:
        size -= 2
    d.text((LEFT, 120), "PIXELMATE", font=font(True, size), fill=YELLOW)
    sub = font(True, 46)
    d.text((LEFT, 300), "Meet Bitsy, a walking pet", font=sub, fill=ORANGE)
    d.text((LEFT, 358), "for the ESP-Mosaico", font=sub, fill=ORANGE)
    body = font(False, 38)
    lines = [
        "Walk to earn sparks.",
        "Unlock six arcade games and new looks.",
        "Snap color photos. Stream a webcam.",
        "Sensor tools and a desk clock.",
        "Keep Bitsy fed by reaching your goal.",
    ]
    for i, line in enumerate(lines):
        d.text((LEFT, 470 + i * 62), line, font=body, fill=TEXT)
    d.text((LEFT, 900), "by Luis Pacheco", font=font(True, 48), fill=YELLOW)
    d.text((LEFT, 962), "www.luigipacheco.com", font=font(False, 38), fill=GREY)
    im.save(IDEAS / "cover.png", optimize=True)
    print(f"cover.png {W}x{H}, title {size}px")


if __name__ == "__main__":
    main()
