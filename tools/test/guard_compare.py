"""Scene sharpness (HUD excluded) and HUD-digit zoom, with vs without the RCAS text guard.

usage: guard_compare.py <dir with sem/ and com/> <capture name>
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

d, name = Path(sys.argv[1]), sys.argv[2]
imgs = {k: Image.open(d / k / f"{name}_full006s.png").convert("RGB") for k in ("sem", "com")}


def sharpness(img, box):
    a = np.asarray(img.crop(box), dtype=np.float64) / 255.0
    y = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
    gx = np.abs(np.diff(y, axis=1)).mean()
    gy = np.abs(np.diff(y, axis=0)).mean()
    return (gx + gy) * 1000


scene = (300, 180, 1500, 700)    # houses, water, boat - no HUD
digits = (1560, 800, 1760, 900)  # speedometer numbers
for k, img in imgs.items():
    print(f"{k} protecao: nitidez da cena={sharpness(img, scene):.2f}  numeros do HUD={sharpness(img, digits):.2f}")
zoom = 4
rows = [(f"{k} protecao", imgs[k].crop(digits).resize(((digits[2] - digits[0]) * zoom, (digits[3] - digits[1]) * zoom), Image.NEAREST))
        for k in ("sem", "com")]
w, h = rows[0][1].size
sheet = Image.new("RGB", (w + 200, 2 * (h + 8)), (20, 20, 20))
for i, (label, im) in enumerate(rows):
    sheet.paste(im, (200, i * (h + 8)))
    ImageDraw.Draw(sheet).text((8, i * (h + 8) + h // 2), label, fill=(255, 255, 255))
sheet.save(d / "zoom_numeros.png")
