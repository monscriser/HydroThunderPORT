"""Compares lossless captures of the same race moment across upscaling paths.

usage: quality_compare.py <dir with nativa_2x_fullNNNs.png, fsr3_..., fsr1_..., bilinear_...>

Writes metrics.json and side-by-side crops (crop_<tag>_<n>.png) into the same dir.
Metrics, per image, on the 3D area (HUD corners excluded):
  sharpness   mean gradient magnitude (higher = more fine detail / crisper edges)
  ssim_vs_ref structural similarity to the native 2x render (1.0 = identical)
  psnr_vs_ref peak signal-to-noise ratio vs native 2x, dB (higher = closer)
  jaggies     high-frequency energy along edges that the reference does not have
              (stair-stepping and ringing; lower = cleaner edges)
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

NAMES = ["nativa_2x", "fsr3", "fsr1", "bilinear", "fsr3_s05", "fsr3_s10", "fsr3_rcas0", "fsr3_s10_rcas0"]
LABELS = {"nativa_2x": "Nativa 2x (referencia)", "fsr3": "FSR 3.1", "fsr1": "FSR 1",
          "bilinear": "Bilinear", "fsr3_s05": "FSR 3.1 + nitidez 0.5",
          "fsr3_s10": "FSR 3.1 + nitidez 1.0", "fsr3_rcas0": "FSR 3.1 + RCAS max",
          "fsr3_s10_rcas0": "FSR 3.1 + nitidez 1.0 + RCAS max"}


def load(path):
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0
    return img


def luma(img):
    return img[..., 0] * 0.299 + img[..., 1] * 0.587 + img[..., 2] * 0.114


def box(a, r):
    # Separable box filter via cumulative sums (window 2r+1), edge-padded.
    p = np.pad(a, r, mode="edge")
    c = np.cumsum(np.cumsum(p, 0), 1)
    c = np.pad(c, ((1, 0), (1, 0)))
    k = 2 * r + 1
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def ssim(x, y, r=3):
    c1, c2 = 0.01 ** 2, 0.03 ** 2
    mx, my = box(x, r), box(y, r)
    vx = box(x * x, r) - mx * mx
    vy = box(y * y, r) - my * my
    cxy = box(x * y, r) - mx * my
    s = ((2 * mx * my + c1) * (2 * cxy + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2))
    return float(s.mean())


def grad(a):
    gx = np.zeros_like(a); gy = np.zeros_like(a)
    gx[:, 1:-1] = (a[:, 2:] - a[:, :-2]) * 0.5
    gy[1:-1, :] = (a[2:, :] - a[:-2, :]) * 0.5
    return np.hypot(gx, gy)


def region(a):
    # Skip HUD corners/text: keep the central band of the 3D scene.
    h, w = a.shape[:2]
    return a[int(h * 0.18):int(h * 0.80), int(w * 0.14):int(w * 0.86)]


def main():
    d = Path(sys.argv[1])
    shots = sorted({p.name.split("_full")[1] for p in d.glob("nativa_2x_full*.png")})
    results = {}
    for s in shots:
        imgs = {n: load(d / f"{n}_full{s}") for n in NAMES if (d / f"{n}_full{s}").exists()}
        if "nativa_2x" not in imgs:
            continue
        ref = luma(region(imgs["nativa_2x"]))
        ref_g = grad(ref)
        per = {}
        for n, img in imgs.items():
            y = luma(region(img))
            g = grad(y)
            mse = float(((y - ref) ** 2).mean())
            # Edge pixels of the reference: compare local high-frequency content
            # there; extra energy relative to the reference = stair-steps/ringing.
            edges = ref_g > np.percentile(ref_g, 90)
            hf = np.abs(y - box(y, 1))
            hf_ref = np.abs(ref - box(ref, 1))
            per[n] = {
                "sharpness": round(float(g.mean()) * 1000, 2),
                "ssim_vs_ref": round(ssim(y, ref), 4),
                "psnr_vs_ref": round(10 * np.log10(1.0 / max(mse, 1e-12)), 2),
                "jaggies": round(float(np.clip(hf[edges] - hf_ref[edges], 0, None).mean()) * 1000, 2),
            }
        results[s] = per
        # Side-by-side crops at 2x zoom from detail-rich spots of the reference.
        h, w = ref.shape
        tiles = 3
        density = box(ref_g, 20)
        picks = []
        for _ in range(tiles):
            iy, ix = np.unravel_index(np.argmax(density), density.shape)
            picks.append((iy, ix))
            density[max(0, iy - 120):iy + 120, max(0, ix - 160):ix + 160] = 0
        cw, ch, zoom = 220, 140, 2
        for t, (iy, ix) in enumerate(picks):
            y0 = int(np.clip(iy - ch // 2, 0, h - ch)); x0 = int(np.clip(ix - cw // 2, 0, w - cw))
            row = Image.new("RGB", (len(imgs) * (cw * zoom + 8), ch * zoom + 28), (20, 20, 20))
            for k, n in enumerate([n for n in NAMES if n in imgs]):
                crop = (region(imgs[n])[y0:y0 + ch, x0:x0 + cw] * 255).astype(np.uint8)
                tile = Image.fromarray(crop).resize((cw * zoom, ch * zoom), Image.NEAREST)
                row.paste(tile, (k * (cw * zoom + 8), 28))
                ImageDraw.Draw(row).text((k * (cw * zoom + 8) + 6, 8), LABELS[n], fill=(255, 255, 255))
            row.save(d / f"crop_{s.replace('.png', '')}_{t}.png")
    (d / "metrics.json").write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
