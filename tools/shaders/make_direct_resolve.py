"""Hydro Thunder port: build the "direct resolve" shaders (ht_direct_resolve).

A resolve first dumped the render target image into the EDRAM buffer (all
samples, Xenia's EDRAM tile layout) and then the SDK resolve copy shader read
that buffer. Here the SDK's own resolve copy shader (decompiled with spirv-cross)
is kept as is, and only its reads of the EDRAM buffer are replaced by a function
that maps the EDRAM sample address back to the render target pixel/sample and
fetches it from the image, packed exactly like the dump shader does
(VulkanRenderTargetCache::GetDumpPipeline):
  kind 0  8_8_8_8 from a float view: uint(c * 255 + 0.5) per channel, R low.
  kind 1  32_FLOAT from a float view: the bits.
  kind 2  32_FLOAT from a uint view: the value.
  kind 3  D24S8: uint(roundEven(depth * 0xFFFFFF)) << 8 | stencil, with the
          40-sample column swap of depth tiles.
Extra push constants (offset 32): render target base tile, pitch in tiles,
resolution scale (x | y << 8).

usage: python3 make_direct_resolve.py <dir with decompiled .comp> <out dir>
"""
import re
import sys
from pathlib import Path

src_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
out_dir.mkdir(parents=True, exist_ok=True)

SHADERS = [  # (decompiled name, multisample variants)
    ("resolve_fast_32bpp_1x2xmsaa", (0,)),
    ("resolve_fast_32bpp_4xmsaa", (1,)),
    ("resolve_full_32bpp", (0, 1)),
]
KINDS = ["8888", "f32", "u32", "d24s8"]


def source_decl(kind, ms):
    t = "texture2DMS" if ms else "texture2D"
    ut = "utexture2DMS" if ms else "utexture2D"
    if kind == 3:
        return (f"layout(set = 0, binding = 0) uniform {t} ht_src;\n"
                f"layout(set = 0, binding = 1) uniform {ut} ht_src_stencil;\n")
    return f"layout(set = 0, binding = 0) uniform {ut if kind == 2 else t} ht_src;\n"


def helper(kind, ms, c):
    fetch_arg = "p, s" if ms else "p, 0"
    swap = ("    uint ht_half = tw >> 1u;\n"
            "    sx = sx < ht_half ? sx + ht_half : sx - ht_half;\n") if kind == 3 else ""
    if ms:
        coords = ("    ivec2 p = ivec2(int(X >> 1u), int(Y >> 1u));\n"
                  "    int s = int((X & 1u) | ((Y & 1u) << 1u));\n")
    else:
        coords = "    ivec2 p = ivec2(int(X), int(Y));\n"
    if kind == 0:
        pack = (f"    vec4 v = texelFetch(ht_src, {fetch_arg});\n"
                "    precise float r = v.x * 255.0 + 0.5;\n"
                "    precise float g = v.y * 255.0 + 0.5;\n"
                "    precise float b = v.z * 255.0 + 0.5;\n"
                "    precise float al = v.w * 255.0 + 0.5;\n"
                "    uint o = uint(r);\n"
                "    o = bitfieldInsert(o, uint(g), 8, 8);\n"
                "    o = bitfieldInsert(o, uint(b), 16, 8);\n"
                "    o = bitfieldInsert(o, uint(al), 24, 8);\n"
                "    return o;\n")
    elif kind == 1:
        pack = f"    return floatBitsToUint(texelFetch(ht_src, {fetch_arg}).x);\n"
    elif kind == 2:
        pack = f"    return texelFetch(ht_src, {fetch_arg}).x;\n"
    else:
        pack = (f"    precise float d = texelFetch(ht_src, {fetch_arg}).x * 16777215.0;\n"
                f"    uint st = texelFetch(ht_src_stencil, {fetch_arg}).x;\n"
                "    return bitfieldInsert(st, uint(roundEven(d)), 8, 24);\n")
    return f"""
// ht: EDRAM sample address -> render target pixel/sample, packed like the dump.
uint ht_edram_sample(uint a)
{{
    uint tw = 80u * ({c}.ht_scale & 0xFFu);
    uint th = 16u * ({c}.ht_scale >> 8u);
    uint tile_size = tw * th;
    uint tile = a / tile_size;
    uint within = a - tile * tile_size;
    uint sy = within / tw;
    uint sx = within - sy * tw;
{swap}    uint rel = (tile - {c}.ht_rt_base) & 2047u;
    uint ty = rel / {c}.ht_rt_pitch;
    uint tx = rel - ty * {c}.ht_rt_pitch;
    uint X = tx * tw + sx;
    uint Y = ty * th + sy;
{coords}{pack}}}

uvec4 ht_edram4(uint i)
{{
    uint a = i * 4u;
    return uvec4(ht_edram_sample(a), ht_edram_sample(a + 1u), ht_edram_sample(a + 2u),
                 ht_edram_sample(a + 3u));
}}

void main()"""


count = 0
for base, ms_list in SHADERS:
    for scaled in (False, True):
        name = base + ("_scaled" if scaled else "")
        src = (src_dir / f"{name}.comp").read_text()
        m = re.search(r"layout\(set = 0, binding = 0, std430\) readonly buffer \w+\s*\{\s*(uint|uvec4) _m0\[\];\s*\}\s*(\w+);", src)
        assert m, name
        elem, edram = m.group(1), m.group(2)
        pc = re.search(r"(layout\(push_constant, std430\) uniform \w+\s*\{)([^}]*)(\}\s*(\w+);)", src)
        assert pc, name
        c = pc.group(4)
        for kind, kind_name in enumerate(KINDS):
            for ms in ms_list:
                s = src.replace(m.group(0), "#extension GL_EXT_samplerless_texture_functions : require\n" +
                                source_decl(kind, ms))
                s = s.replace(pc.group(0), pc.group(1) + pc.group(2) +
                              "    layout(offset = 32) uint ht_rt_base;\n    uint ht_rt_pitch;\n    uint ht_scale;\n" +
                              pc.group(3))
                assert s.count("void main()") == 1
                s = s.replace("void main()", helper(kind, ms, c).lstrip("\n"), 1)
                fn = "ht_edram4" if elem == "uvec4" else "ht_edram_sample"
                s, n = re.subn(rf"{edram}\._m0\[([^\[\]]+)\]", rf"{fn}(\1)", s)
                assert n > 0 and edram + "._m0" not in s, name
                # The #extension must follow #version.
                s = s.replace("#version 450\n", "#version 450\n#extension GL_EXT_samplerless_texture_functions : require\n", 1)
                s = s.replace("#extension GL_EXT_samplerless_texture_functions : require\n" + source_decl(kind, ms),
                              source_decl(kind, ms), 1)
                out = f"ht_direct_{name.replace('resolve_', '')}_{kind_name}{'_ms' if ms else ''}.cs.glsl"
                header = ("// Generated by tools/shaders/make_direct_resolve.py from the SDK shader "
                          f"{name} (spirv-cross). Do not edit.\n")
                (out_dir / out).write_text(header + s)
                count += 1
print("generated", count)
