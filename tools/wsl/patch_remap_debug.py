"""Temporary: log why 4x clear rectangles are not remapped ([ht-remap-why])."""
import sys
from pathlib import Path

cp = Path(sys.argv[1]) / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
old = "    if (surface_info.msaa_samples == xenos::MsaaSamples::k4X &&\n        initiator.prim_type == xenos::PrimitiveType::kRectangleList && initiator.num_indices == 3 &&"
assert old in s
if "[ht-remap-why]" not in s:
    s = s.replace(old, '''    if (surface_info.msaa_samples == xenos::MsaaSamples::k4X &&
        initiator.prim_type == xenos::PrimitiveType::kRectangleList) {
      static uint32_t ht_why_count = 0;
      if (ht_why_count < 40) {
        ++ht_why_count;
        REXGPU_WARN(
            "[ht-remap-why] n={} src={} vte={:X} xyfmt={} a2m={} at={} winoff={} poly={}{}{} aa={:X} "
            "z={} zfunc={} ps={} interp={:X} pgen={} tex={} oDepth={} kill={} owner={}",
            uint32_t(initiator.num_indices), uint32_t(initiator.source_select), vte.value & 0x3F,
            uint32_t(vte.vtx_xy_fmt), uint32_t(color_control.alpha_to_mask_enable),
            uint32_t(color_control.alpha_test_enable), uint32_t(mode_cntl.vtx_window_offset_enable),
            uint32_t(mode_cntl.poly_offset_front_enable), uint32_t(mode_cntl.poly_offset_back_enable),
            uint32_t(mode_cntl.poly_offset_para_enable), regs[XE_GPU_REG_PA_SC_AA_MASK],
            uint32_t(normalized_depth_control.z_enable), uint32_t(normalized_depth_control.zfunc),
            pixel_shader != nullptr, interpolator_mask, ps_param_gen_pos,
            pixel_shader ? pixel_shader->texture_bindings().size() : 0,
            pixel_shader ? pixel_shader->writes_depth() : false,
            pixel_shader ? pixel_shader->kills_pixels() : false,
            render_target_cache_->HtClearCanUse1x(normalized_depth_control, ht_color_mask,
                                                  *vertex_shader));
      }
    }
''' + old, 1)
    cp.write_text(s)
print("patched")
