"""4x zoom of the small speedometer digits, one row per capture.

usage: text_zoom.py <dir> <out.png> <name>...
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

d = Path(sys.argv[1])
box = (1560, 800, 1760, 900)  # "80", "120", "160" on the speedometer
zoom = 4
rows = []
for n in sys.argv[3:]:
    img = Image.open(d / f"{n}_full006s.png").convert("RGB").crop(box)
    rows.append((n, img.resize((img.width * zoom, img.height * zoom), Image.NEAREST)))
w, h = rows[0][1].size
sheet = Image.new("RGB", (w + 230, len(rows) * (h + 8)), (20, 20, 20))
for i, (n, img) in enumerate(rows):
    sheet.paste(img, (230, i * (h + 8)))
    ImageDraw.Draw(sheet).text((8, i * (h + 8) + h // 2), n, fill=(255, 255, 255))
sheet.save(sys.argv[2])
print("ok", sheet.size)
