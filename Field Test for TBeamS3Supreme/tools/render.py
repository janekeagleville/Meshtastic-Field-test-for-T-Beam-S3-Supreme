#!/usr/bin/env python3
"""Sklada podglady PNG ekranu OLED 128x64 z zapisow symulatora (shots/*.pbm).
Obraz jest wierny plytce: symulator rysuje prawdziwa biblioteka u8g2 z tymi samymi czcionkami.
Uzycie: python3 tools/render.py shots   (opcjonalnie --sheet: zbiorczy arkusz shots/sheet.png)"""
import sys, glob, os
from PIL import Image, ImageDraw
S = 4
ON, OFF, GAP = (235, 242, 255), (8, 10, 16), (2, 3, 6)
def load(path):
    t = open(path).read().split()
    assert t[0] == "P1"
    w, h = int(t[1]), int(t[2])
    bits = "".join(t[3:])
    return w, h, bits
def render(path):
    w, h, bits = load(path)
    im = Image.new("RGB", (w * S, h * S), GAP)
    d = ImageDraw.Draw(im)
    for y in range(h):
        for x in range(w):
            c = ON if bits[y * w + x] == "1" else OFF
            d.rectangle([x * S, y * S, x * S + S - 2, y * S + S - 2], fill=c)
    return im
if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "shots"
    files = sorted(glob.glob(os.path.join(src, "*.pbm")))
    ims = []
    for f in files:
        im = render(f)
        im.save(f[:-4] + ".png")
        ims.append((os.path.basename(f)[:-4], im))
    if "--sheet" in sys.argv and ims:
        cols = 3
        cw, ch = 128 * S + 24, 64 * S + 44
        rows = (len(ims) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cw, rows * ch), (30, 32, 38))
        d = ImageDraw.Draw(sheet)
        for i, (n, im) in enumerate(ims):
            x, y = (i % cols) * cw + 12, (i // cols) * ch + 30
            sheet.paste(im, (x, y))
            d.text((x, y - 18), n, fill=(200, 205, 215))
        sheet.save(os.path.join(src, "sheet.png"))
