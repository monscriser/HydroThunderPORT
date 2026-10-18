"""Side-by-side 2x crops of HUD text from 1920x1080 captures.

usage: text_crops.py <dir> <out.png> <name>... (files <name>_full006s.png)
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

d = Path(sys.argv[1])
names = sys.argv[3:]
regions = {"Tempo": (95, 75, 480, 160), "Posicao": (1390, 75, 1830, 160), "Velocimetro": (1550, 770, 1810, 1010)}
zoom, pad = 2, 10
cols = []
for n in names:
    img = Image.open(d / f"{n}_full006s.png").convert("RGB")
    tiles = [img.crop(box).resize(((box[2] - box[0]) * zoom, (box[3] - box[1]) * zoom), Image.NEAREST)
             for box in regions.values()]
    cols.append((n, tiles))
w = max(t.width for _, ts in cols for t in ts)
h = sum(t.height for t in cols[0][1]) + pad * len(regions) + 30
sheet = Image.new("RGB", (len(cols) * (w + pad), h), (20, 20, 20))
for i, (n, tiles) in enumerate(cols):
    x = i * (w + pad)
    ImageDraw.Draw(sheet).text((x + 6, 8), n, fill=(255, 255, 255))
    y = 30
    for t in tiles:
        sheet.paste(t, (x, y))
        y += t.height + pad
sheet.save(sys.argv[2])
print("ok", sys.argv[2], sheet.size)
