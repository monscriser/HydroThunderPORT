"""Test knobs for ht_clear_transfer_cutout:
  ht_clear_cutout_slots (int, default 31): bit 0 depth, bits 1-4 color targets.
  ht_clear_cutout_depth_needs_stencil (bool, default false): the depth target
    only counts as cleared when the clear also writes the whole stencil.
usage: python3 patch_clear_cutout_knobs.py /root/ht/sdk"""
import sys
from pathlib import Path
vk = Path(sys.argv[1]) / "src/graphics/vulkan/render_target_cache.cpp"
s = vk.read_text()
if "ht_clear_cutout_slots" not in s:
    old = 'REXCVAR_DEFINE_BOOL(ht_clear_transfer_cutout, true, "GPU",'
    assert old in s
    s = s.replace(old, 'REXCVAR_DEFINE_INT32(ht_clear_cutout_slots, 31, "GPU",\n'
                  '                     "Test: targets the clear cutout may apply to (bit 0 depth, 1-4 color)");\n'
                  'REXCVAR_DEFINE_BOOL(ht_clear_cutout_depth_needs_stencil, false, "GPU",\n'
                  '                    "Test: depth counts as cleared only if the clear writes all stencil");\n' + old, 1)
    old = """  if (rts[0] && normalized_depth_control.z_enable && normalized_depth_control.z_write_enable &&
      normalized_depth_control.zfunc == xenos::CompareFunction::kAlways) {
    slots |= 1;
  }"""
    assert old in s
    s = s.replace(old, """  const bool ht_stencil_cleared =
      normalized_depth_control.stencil_enable && !normalized_depth_control.backface_enable &&
      normalized_depth_control.stencilfunc == xenos::CompareFunction::kAlways &&
      normalized_depth_control.stencilzpass == xenos::StencilOp::kReplace &&
      ((regs[XE_GPU_REG_RB_STENCILREFMASK] >> 16) & 0xFF) == 0xFF;
  if (rts[0] && normalized_depth_control.z_enable && normalized_depth_control.z_write_enable &&
      normalized_depth_control.zfunc == xenos::CompareFunction::kAlways &&
      (ht_stencil_cleared || !REXCVAR_GET(ht_clear_cutout_depth_needs_stencil))) {
    slots |= 1;
  }""", 1)
    old = "  float r[4];\n  if (!slots || !HtRectangleBounds(vertex_shader, r)) {"
    assert old in s
    s = s.replace(old, "  slots &= uint32_t(REXCVAR_GET(ht_clear_cutout_slots));\n" + old, 1)
    vk.write_text(s)
print("patched")
