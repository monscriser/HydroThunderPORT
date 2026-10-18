"""Diagnostic (frame trace only): log the 3 clear-rectangle vertices computed on
the CPU and the vertex shader disassembly once per shader, to understand the
odd bounds of HtRectangleBounds (x 480-512 for a 512-wide clear).

usage: python3 patch_rect_debug.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])
est = root / "src/graphics/util/draw_extent_estimator.cpp"
s = est.read_text()
if "[ht-trace]     v" not in s:
    old = """    for (uint32_t a = 0; a < 2; ++a) {
      const float v = rex::memory::Reinterpret<float>(sink.values[16][a]) * scale[a] + offset[a];"""
    assert old in s
    s = s.replace(old, """    REXGPU_WARN("[ht-trace]     v{} idx={} pos=({}, {}, {}, {}) mask={:X} scale=({},{}) off=({},{})", i,
                std::min(max_index, std::max(min_index, (i + index_offset) & 0xFFFFFF)),
                rex::memory::Reinterpret<float>(sink.values[16][0]),
                rex::memory::Reinterpret<float>(sink.values[16][1]),
                rex::memory::Reinterpret<float>(sink.values[16][2]),
                rex::memory::Reinterpret<float>(sink.values[16][3]), sink.masks[16], scale[0],
                scale[1], offset[0], offset[1]);
""" + old, 1)
    if "#include <rex/logging.h>" not in s:
        s = s.replace("namespace rex::graphics {", "#include <rex/logging.h>\n\nnamespace rex::graphics {", 1)
    est.write_text(s)

cache = root / "src/graphics/pipeline/render_target/cache.cpp"
c = cache.read_text()
if "[ht-trace] vs disasm" not in c:
    old = """      if (ht_has_rect) {
        REXGPU_WARN("[ht-trace]   rect=({},{})-({},{}) pitch_px={}", ht_rect[0], ht_rect[1],"""
    assert old in c
    c = c.replace(old, """      {
        static std::set<uint64_t> ht_dumped;
        if (ht_has_rect && ht_dumped.insert(vertex_shader.ucode_data_hash()).second) {
          REXGPU_WARN("[ht-trace] vs disasm 0x{:016X} min={} max={} offset={}:\\n{}",
                      vertex_shader.ucode_data_hash(), regs[XE_GPU_REG_VGT_MIN_VTX_INDX],
                      regs[XE_GPU_REG_VGT_MAX_VTX_INDX], regs[XE_GPU_REG_VGT_INDX_OFFSET],
                      vertex_shader.ucode_disassembly());
        }
      }
""" + old, 1)
    if "#include <set>" not in c:
        c = c.replace("#include <rex/graphics/pipeline/render_target/cache.h>",
                      "#include <set>\n\n#include <rex/graphics/pipeline/render_target/cache.h>", 1)
    cache.write_text(c)
print("patched")
