"""Side-by-side crops of the same region from several runs.
usage: crop_side.py <out.png> x0 y0 x1 y1 <img1> <img2> ..."""
import sys
from PIL import Image
out, x0, y0, x1, y1 = sys.argv[1], *map(int, sys.argv[2:6])
ims = [Image.open(p).convert("RGB").crop((x0, y0, x1, y1)) for p in sys.argv[6:]]
w, h = ims[0].size
canvas = Image.new("RGB", (w, h * len(ims)))
for i, im in enumerate(ims):
    canvas.paste(im, (0, i * h))
canvas.save(out)
