"""Adds a text/HUD guard to the decompiled RCAS shaders.

RCAS adds a negative lobe (_20919) scaled by the sharpness. The game draws its HUD
and menu text into the same 720p frame as the scene, so after upscaling, thin
letters with dark outlines get doubled/ringing edges. Text has a much higher local
contrast (max - min of the 5-tap cross) than scene detail, so the lobe fades to
zero between LOW and HIGH contrast; textures and soft edges keep full sharpening.

usage: guard_rcas.py <in.frag> <out.frag> <low> <high>
"""
import sys

src, dst, low, high = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
s = open(src).read()
anchor = "    float _16498 = (4.0 * _20919) + 1.0;\n"
assert s.count(anchor) == 1, "lobe anchor not found"
guard = (
    "    // htrecomp: text/HUD guard - no sharpening across very high local contrast.\n"
    "    float ht_contrast = max(max(_21851 - _15948, _21854 - _15951), _10142 - _10905);\n"
    f"    _20919 *= 1.0 - smoothstep({low:.3f}, {high:.3f}, ht_contrast);\n"
)
s = s.replace(anchor, guard + anchor)
open(dst, "w").write(s)
print("ok", dst, low, high)
