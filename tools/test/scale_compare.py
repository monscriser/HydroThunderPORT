"""Compares lossless standing shots of different render scales against Nativa.

usage: scale_compare.py <dir with img-nativa/, img-qualidade/, img-2x1/ ...> [shot]

Only static regions are scored (rocks, wind turbines, sky, the boat); the water
moves between runs. Writes metrics.json and side-by-side crops into <dir>.
  ssim      structural similarity to Nativa (1.0 = identical)
  detail    mean gradient magnitude relative to Nativa (1.0 = same fine detail)
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

# (name, x0, y0, x1, y1) in 1920x1080 shots.
REGIONS = [("rochas_moinhos", 760, 120, 1880, 420), ("barco", 560, 600, 1360, 1000),
           ("horizonte_esq", 0, 160, 760, 420)]


def luma(img):
    return img[..., 0] * 0.299 + img[..., 1] * 0.587 + img[..., 2] * 0.114


def box(a, r=3):
    p = np.pad(a, r, mode="edge")
    c = np.pad(np.cumsum(np.cumsum(p, 0), 1), ((1, 0), (1, 0)))
    n = 2 * r + 1
    return (c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]) / (n * n)


def ssim(a, b):
    c1, c2 = 0.01 ** 2, 0.03 ** 2
    ma, mb = box(a), box(b)
    va, vb, cov = box(a * a) - ma * ma, box(b * b) - mb * mb, box(a * b) - ma * mb
    return float(np.mean(((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))))


def detail(a):
    gy, gx = np.gradient(a)
    return float(np.mean(np.hypot(gx, gy)))


def main():
    root = Path(sys.argv[1])
    shot = sys.argv[2] if len(sys.argv) > 2 else "still-3.png"
    names = sorted(p.name for p in root.iterdir() if p.is_dir() and (p / shot).exists())
    imgs = {n: np.asarray(Image.open(root / n / shot).convert("RGB"), dtype=np.float64) / 255.0 for n in names}
    ref = "img-nativa"
    out = {}
    for name, img in imgs.items():
        out[name] = {}
        for reg, x0, y0, x1, y1 in REGIONS:
            a, r = luma(img[y0:y1, x0:x1]), luma(imgs[ref][y0:y1, x0:x1])
            out[name][reg] = {"ssim": round(ssim(a, r), 4), "detail": round(detail(a) / detail(r), 3)}
    (root / "metrics.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
    for reg, x0, y0, x1, y1 in REGIONS:
        w, h = x1 - x0, y1 - y0
        sheet = Image.new("RGB", (w, (h + 28) * len(names)), "black")
        draw = ImageDraw.Draw(sheet)
        for i, name in enumerate(names):
            crop = Image.fromarray((imgs[name][y0:y1, x0:x1] * 255).astype(np.uint8))
            sheet.paste(crop, (0, i * (h + 28) + 28))
            m = out[name][reg]
            draw.text((6, i * (h + 28) + 6), f"{name}  ssim={m['ssim']}  detalhe={m['detail']}", fill="yellow")
        sheet.save(root / f"crop_{reg}.png")
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
