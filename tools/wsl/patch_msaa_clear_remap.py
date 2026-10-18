"""Draw 4x MSAA Direct3D clears on the 1x render targets that own the EDRAM.

Hydro Thunder clears each shadow map with one 4x MSAA rectangle (a cheap clear
on the Xbox 360) and then draws it at 1x over the same EDRAM. Host render
targets then copy the whole surface 1x->4x before the clear and 4x->1x after
it, for depth/stencil and color: about 20 of 26 transfers per frame.

A 4x pixel covers exactly the 2x2 samples that 1x pixels cover at the same EDRAM
addresses. For a constant-output rectangle (no textures, no interpolators, no
kill, no oDepth, constant Z, all samples enabled), drawing it at 1x with the
rectangle scaled by 2 (and the half-pixel offset doubled) writes the same
values to the same samples, and untouched channels (stencil here) simply stay.
Used only when the EDRAM is owned by the matching 1x targets, so no transfers
are needed at all. cvar ht_msaa_clear_remap.

usage: python3 patch_msaa_clear_remap.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new):
    s = path.read_text()
    assert old in s, (path, old[:80])
    path.write_text(s.replace(old, new, 1))


ch = root / "include/rex/graphics/pipeline/render_target/cache.h"
if "HtClearCanUse1x" not in ch.read_text():
    patch(ch, "  bool WouldOwnershipChangeRequireTransfers(RenderTargetKey dest,",
          """ public:
  // ht: whether a 4x MSAA clear rectangle touches only EDRAM that the matching
  // 1x render targets own (see ht_msaa_clear_remap in the command processor).
  bool HtClearCanUse1x(reg::RB_DEPTHCONTROL normalized_depth_control,
                       uint32_t normalized_color_mask, const Shader& vertex_shader);

 private:
  bool WouldOwnershipChangeRequireTransfers(RenderTargetKey dest,""")

rc = root / "src/graphics/pipeline/render_target/cache.cpp"
if "RenderTargetCache::HtClearCanUse1x" not in rc.read_text():
    patch(rc, "bool RenderTargetCache::WouldOwnershipChangeRequireTransfers(RenderTargetKey dest,",
          """bool RenderTargetCache::HtClearCanUse1x(reg::RB_DEPTHCONTROL normalized_depth_control,
                                        uint32_t normalized_color_mask,
                                        const Shader& vertex_shader) {
  if (GetPath() != Path::kHostRenderTargets) {
    return false;
  }
  const RegisterFile& regs = register_file();
  auto surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  if (surface_info.msaa_samples != xenos::MsaaSamples::k4X || !surface_info.surface_pitch) {
    return false;
  }
  // 4x has 2 horizontal samples: the 1x pitch in pixels is twice as large.
  const uint32_t pitch_tiles_at_32bpp =
      ((surface_info.surface_pitch << 1) + (xenos::kEdramTileWidthSamples - 1)) /
      xenos::kEdramTileWidthSamples;
  const uint32_t height_4x = std::min(GetRenderTargetHeight(pitch_tiles_at_32bpp, xenos::MsaaSamples::k4X),
                                      draw_extent_estimator_.EstimateMaxY(true, vertex_shader));
  const uint32_t length_tiles =
      ((height_4x << 1) + (xenos::kEdramTileHeightSamples - 1)) / xenos::kEdramTileHeightSamples *
      pitch_tiles_at_32bpp;
  if (!length_tiles || length_tiles > xenos::kEdramTileCount) {
    return false;
  }
  bool any = false;
  auto saves = [&](RenderTargetKey key_4x) {
    any = true;
    key_4x.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    key_4x.msaa_samples = xenos::MsaaSamples::k4X;
    RenderTargetKey key_1x = key_4x;
    key_1x.msaa_samples = xenos::MsaaSamples::k1X;
    return !WouldOwnershipChangeRequireTransfers(key_1x, 0, length_tiles) &&
           WouldOwnershipChangeRequireTransfers(key_4x, 0, length_tiles);
  };
  if (normalized_depth_control.z_enable || normalized_depth_control.stencil_enable) {
    auto depth_info = regs.Get<reg::RB_DEPTH_INFO>();
    RenderTargetKey key;
    key.base_tiles = depth_info.depth_base;
    key.is_depth = 1;
    key.resource_format = uint32_t(depth_info.depth_format);
    if (!saves(key)) {
      return false;
    }
  }
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    if (!(normalized_color_mask & (uint32_t(0b1111) << (4 * i)))) {
      continue;
    }
    auto color_info = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    RenderTargetKey key;
    key.base_tiles = color_info.color_base;
    key.resource_format = uint32_t(
        GetColorResourceFormat(xenos::GetStorageColorFormat(color_info.color_format)));
    if (!saves(key)) {
      return false;
    }
  }
  return any;
}

bool RenderTargetCache::WouldOwnershipChangeRequireTransfers(RenderTargetKey dest,""")

cp = root / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
if "ht_msaa_clear_remap" not in s:
    first_cvar = s.index("REXCVAR_DEFINE_")
    s = s[:first_cvar] + '''REXCVAR_DEFINE_BOOL(ht_msaa_clear_remap, false, "GPU",
                    "Draw 4x MSAA clear rectangles on the 1x render targets that own the "
                    "EDRAM (no ownership transfers, same EDRAM contents)");

''' + s[first_cvar:]
    cp.write_text(s)
    patch(cp, """  PrimitiveProcessor::ProcessingResult primitive_processing_result;
  SpirvShaderTranslator::Modification vertex_shader_modification;""",
          """  // ht: 4x MSAA clear rectangle over EDRAM owned by 1x targets - draw it on
  // those targets with the rectangle scaled by 2 (see ht_msaa_clear_remap).
  struct HtRegisterRestore {
    RegisterFile* file = nullptr;
    std::array<std::pair<uint32_t, uint32_t>, 10> saved{};
    uint32_t count = 0;
    void Set(uint32_t reg, uint32_t value) {
      saved[count++] = {reg, file->values[reg]};
      file->values[reg] = value;
    }
    ~HtRegisterRestore() {
      while (count) {
        --count;
        file->values[saved[count].first] = saved[count].second;
      }
    }
  } ht_restore;
  if (REXCVAR_GET(ht_msaa_clear_remap) && is_rasterization_done && !memexport_used_vertex && !memexport_used_pixel &&
      edram_mode == xenos::EdramMode::kColorDepth) {
    auto surface_info = regs.Get<reg::RB_SURFACE_INFO>();
    auto initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
    auto vte = regs.Get<reg::PA_CL_VTE_CNTL>();
    auto color_control = regs.Get<reg::RB_COLORCONTROL>();
    auto mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
    const uint32_t ht_color_mask =
        pixel_shader ? draw_util::GetNormalizedColorMask(regs, pixel_shader->writes_color_targets())
                     : 0;
    if (surface_info.msaa_samples == xenos::MsaaSamples::k4X &&
        initiator.prim_type == xenos::PrimitiveType::kRectangleList && initiator.num_indices == 3 &&
        initiator.source_select == xenos::SourceSelect::kAutoIndex && !(vte.value & 0x3F) &&
        vte.vtx_xy_fmt && !color_control.alpha_to_mask_enable && !color_control.alpha_test_enable &&
        !mode_cntl.vtx_window_offset_enable && !mode_cntl.poly_offset_front_enable &&
        !mode_cntl.poly_offset_back_enable && !mode_cntl.poly_offset_para_enable &&
        (regs[XE_GPU_REG_PA_SC_AA_MASK] & 0xF) == 0xF &&
        (!normalized_depth_control.z_enable ||
         normalized_depth_control.zfunc == xenos::CompareFunction::kAlways) &&
        (!pixel_shader || (!interpolator_mask && ps_param_gen_pos == UINT32_MAX &&
                           pixel_shader->texture_bindings().empty() &&
                           !pixel_shader->writes_depth() && !pixel_shader->kills_pixels())) &&
        render_target_cache_->HtClearCanUse1x(normalized_depth_control, ht_color_mask,
                                              *vertex_shader)) {
      ht_restore.file = register_file_;
      // Same EDRAM rows and columns at 1x: twice the pitch in pixels.
      auto surface_info_1x = surface_info;
      surface_info_1x.msaa_samples = xenos::MsaaSamples::k1X;
      surface_info_1x.surface_pitch = surface_info.surface_pitch << 1;
      ht_restore.Set(XE_GPU_REG_RB_SURFACE_INFO, surface_info_1x.value);
      // Screen-space positions times 2. The D3D half-pixel offset is one 1x
      // pixel at 4x, and the viewport code adds another half pixel.
      const bool half_pixel = REXCVAR_GET(half_pixel_offset) &&
                              regs.Get<reg::PA_SU_VTX_CNTL>().pix_center ==
                                  xenos::PixelCenter::kD3DZero;
      auto vte_1x = vte;
      vte_1x.vport_x_scale_ena = 1;
      vte_1x.vport_x_offset_ena = 1;
      vte_1x.vport_y_scale_ena = 1;
      vte_1x.vport_y_offset_ena = 1;
      ht_restore.Set(XE_GPU_REG_PA_CL_VTE_CNTL, vte_1x.value);
      const float two = 2.0f, offset = half_pixel ? 0.5f : 0.0f;
      uint32_t two_bits, offset_bits;
      std::memcpy(&two_bits, &two, sizeof(two_bits));
      std::memcpy(&offset_bits, &offset, sizeof(offset_bits));
      ht_restore.Set(XE_GPU_REG_PA_CL_VPORT_XSCALE, two_bits);
      ht_restore.Set(XE_GPU_REG_PA_CL_VPORT_XOFFSET, offset_bits);
      ht_restore.Set(XE_GPU_REG_PA_CL_VPORT_YSCALE, two_bits);
      ht_restore.Set(XE_GPU_REG_PA_CL_VPORT_YOFFSET, offset_bits);
      // Scissors are in pixels too.
      auto scale_xy = [](uint32_t value) {
        const uint32_t x = std::min((value & 0x7FFF) << 1, uint32_t(0x7FFF));
        const uint32_t y = std::min(((value >> 16) & 0x7FFF) << 1, uint32_t(0x7FFF));
        return (value & 0x80008000) | x | (y << 16);
      };
      for (uint32_t reg : {XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR,
                           XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR}) {
        ht_restore.Set(reg, scale_xy(regs[reg]));
      }
      ++perf_counters_.ht_clear_remaps;
    }
  }

  PrimitiveProcessor::ProcessingResult primitive_processing_result;
  SpirvShaderTranslator::Modification vertex_shader_modification;""")

h = root / "include/rex/graphics/vulkan/command_processor.h"
if "ht_clear_remaps" not in h.read_text():
    patch(h, "    uint64_t draws = 0;\n", "    uint64_t draws = 0;\n    uint64_t ht_clear_remaps = 0;\n")
    patch(cp, 'rp_breaks_barrier={:.1f}",', 'rp_breaks_barrier={:.1f} clear_remaps={:.1f}",')
    patch(cp, "      double(c.rp_breaks_barrier) / f);", "      double(c.rp_breaks_barrier) / f, double(c.ht_clear_remaps) / f);")
print("patched")
