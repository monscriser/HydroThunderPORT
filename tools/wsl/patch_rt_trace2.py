"""One-off diagnostics, after patch_rt_trace.py: adds draw state to the
[ht-trace] bind lines (primitive, depth/stencil, color mask, blend, scissor).

usage: python3 patch_rt_trace2.py /root/ht/sdk
"""
import sys
from pathlib import Path

rc = Path(sys.argv[1]) / "src/graphics/pipeline/render_target/cache.cpp"
s = rc.read_text()
old = '''      REXGPU_WARN("[ht-trace] draw {} bind{} h={}", uint64_t(ht_trace_draw), bound, height_used);'''
new = '''      REXGPU_WARN(
          "[ht-trace] draw {} bind{} h={} | init=0x{:08X} dma=0x{:08X} depthctl=0x{:08X} "
          "cmask=0x{:08X} rawcmask=0x{:08X} blend0=0x{:08X} colorctl=0x{:08X} stencil=0x{:08X} "
          "scissor=0x{:08X}-0x{:08X} winoff=0x{:08X} vte=0x{:08X} vp_x={}/{} vp_y={}/{} vs=0x{:016X}",
          uint64_t(ht_trace_draw), bound, height_used, regs[XE_GPU_REG_VGT_DRAW_INITIATOR],
          regs[XE_GPU_REG_VGT_DMA_SIZE], normalized_depth_control.value, normalized_color_mask,
          regs[XE_GPU_REG_RB_COLOR_MASK], regs[XE_GPU_REG_RB_BLENDCONTROL0],
          regs[XE_GPU_REG_RB_COLORCONTROL], regs[XE_GPU_REG_RB_STENCILREFMASK],
          regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL], regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR],
          regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET], regs[XE_GPU_REG_PA_CL_VTE_CNTL],
          regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XSCALE), regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XOFFSET),
          regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE), regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET),
          vertex_shader.ucode_data_hash());'''
if "rawcmask" not in s:
    assert old in s
    rc.write_text(s.replace(old, new, 1))
print("patched")
