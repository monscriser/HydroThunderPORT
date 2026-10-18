"""Measurement only (may change the image of mipmapped / anisotropic textures):
HT_MEASURE_FETCH_LOD0=1 makes translated pixel shader texture fetches sample with
an explicit LOD instead of textureGrad, to measure the cost of the gradient path.
Shaders already translated in this process are not affected (env read at startup).

usage: python3 patch_measure_fetch_lod0.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])
f = root / "src/graphics/pipeline/shader/spirv_translator_fetch.cpp"
s = f.read_text()
old = """    bool use_computed_lod = instr.attributes.use_computed_lod &&
                            (is_pixel_shader() || instr.attributes.use_register_gradients);"""
new = """    static const bool ht_measure_lod0 = std::getenv("HT_MEASURE_FETCH_LOD0") != nullptr;
    bool use_computed_lod = instr.attributes.use_computed_lod &&
                            (is_pixel_shader() || instr.attributes.use_register_gradients) &&
                            !(ht_measure_lod0 && instr.opcode == ucode::FetchOpcode::kTextureFetch &&
                              !instr.attributes.use_register_gradients);"""
assert s.count(old) == 1
s = s.replace(old, new)
if "#include <cstdlib>" not in s:
    s = s.replace("#include <cstdint>", "#include <cstdint>\n#include <cstdlib>", 1) if "#include <cstdint>" in s else "#include <cstdlib>\n" + s
f.write_text(s)
print("patched measure fetch lod0")
