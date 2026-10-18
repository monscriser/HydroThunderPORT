"""Diagnostic (frame trace only): log every screen-space rectangle draw (clears)
with its bounds, not only the ones that change bindings.
usage: python3 patch_rect_debug2.py /root/ht/sdk"""
import sys
from pathlib import Path
cache = Path(sys.argv[1]) / "src/graphics/pipeline/render_target/cache.cpp"
c = cache.read_text()
if "[ht-trace] rectdraw" not in c:
    old = "    if (bound != ht_last || any_transfer) {\n      float ht_rect[4] = {};"
    assert old in c
    c = c.replace(old, """    if ((regs[XE_GPU_REG_PA_CL_VTE_CNTL] & 0x100) &&
        (regs[XE_GPU_REG_VGT_DRAW_INITIATOR] & 0x3F) == uint32_t(xenos::PrimitiveType::kRectangleList)) {
      float r[4] = {};
      if (draw_extent_estimator_.HtRectangleBounds(vertex_shader, r)) {
        REXGPU_WARN("[ht-trace] rectdraw {} rect=({},{})-({},{}) offset={} vs=0x{:016X} depthctl=0x{:08X} cmask=0x{:08X}",
                    uint64_t(ht_trace_draw), r[0], r[1], r[2], r[3], regs[XE_GPU_REG_VGT_INDX_OFFSET],
                    vertex_shader.ucode_data_hash(), normalized_depth_control.value, normalized_color_mask);
      }
    }
""" + old, 1)
    cache.write_text(c)
print("patched")
