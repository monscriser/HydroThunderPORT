"""Measurement only (breaks anything that reads the resolved memory): cvar
ht_resolve_texture_measure_skip_buffer makes the resolve-to-texture shaders skip
their destination buffer stores, to measure what a lazy memory write could save.
Apply on top of patch_resolve_texture.py; revert from /root/sdk-backup-rttmeasure.

usage: python3 patch_rtt_measure_skip.py /root/ht/sdk <dir with ht_rtt_*.cs.glsl>
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(sys.argv[1])
glsl_dir = Path(sys.argv[2])
spirv_dir = root / "src/graphics/shaders/vulkan_spirv"
tmp = Path(tempfile.mkdtemp())
for f in sorted(glsl_dir.glob("ht_rtt_*.cs.glsl")):
    s = f.read_text()
    s, n = re.subn(r"^(\s*)(_5522\._m0\[.*\] = _\d+;)$", r"\1if ((_3305.ht_tex_info & 512u) == 0u) \2",
                   s, flags=re.M)
    assert n in (1, 2), (f, n)
    out = tmp / f.name
    out.write_text(s)
    var = f.name.replace(".cs.glsl", "_cs")
    subprocess.run(["glslangValidator", "-V", "-S", "comp", "--target-env", "vulkan1.0", "--vn", var,
                    "-o", str(spirv_dir / f"{var}.h"), str(out)], check=True, stdout=subprocess.DEVNULL)

vk = root / "src/graphics/vulkan/render_target_cache.cpp"
s = vk.read_text()
old = "                          (REXCVAR_GET(ht_resolve_texture_verify_break) ? UINT32_C(256) : 0);"
assert s.count(old) == 1
s = s.replace(old, "                          (REXCVAR_GET(ht_resolve_texture_verify_break) ? UINT32_C(256) : 0) |\n"
              "                          (REXCVAR_GET(ht_resolve_texture_measure_skip_buffer) ? UINT32_C(512) : 0);")
old = 'REXCVAR_DEFINE_BOOL(ht_resolve_texture_verify_break, false, "GPU",'
assert s.count(old) == 1
s = s.replace(old, 'REXCVAR_DEFINE_BOOL(ht_resolve_texture_measure_skip_buffer, false, "GPU",\n'
              '                    "Measurement only: resolve-to-texture skips the memory stores (breaks readers)");\n' + old)
vk.write_text(s)
print("patched measure skip")
