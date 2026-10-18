"""Follow-up to patch_msaa_clear_remap.py (apply after it):
- prove the clear is constant: run the vertex shader of the 3 rectangle
  vertices on the CPU and require identical Z/W and identical values for every
  interpolator the pixel shader reads (replaces "no interpolators");
- allow vtx_window_offset_enable when the window offset is 0;
- clamp each render target's range at the next bound render target, like
  RenderTargetCache::Update does, before asking about ownership.

usage: python3 patch_msaa_clear_remap_v2.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new):
    s = path.read_text()
    assert old in s, (path, old[:90])
    path.write_text(s.replace(old, new, 1))


eh = root / "include/rex/graphics/util/draw_extent_estimator.h"
if "HtRectangleExportsConstant" not in eh.read_text():
    patch(eh, "  uint32_t EstimateVertexMaxY(const Shader& vertex_shader);",
          """  uint32_t EstimateVertexMaxY(const Shader& vertex_shader);
  // ht: for an auto-indexed 3-vertex rectangle, true if every vertex exports
  // the same position Z/W and the same values for the interpolators in
  // interpolator_mask, i.e. the rectangle is drawn with constant attributes.
  bool HtRectangleExportsConstant(const Shader& vertex_shader, uint32_t interpolator_mask);""")

ec = root / "src/graphics/util/draw_extent_estimator.cpp"
if "HtRectangleExportsConstant" not in ec.read_text():
    patch(ec, "uint32_t DrawExtentEstimator::EstimateVertexMaxY(const Shader& vertex_shader) {",
          """namespace {
class HtExportCaptureSink : public ShaderInterpreter::ExportSink {
 public:
  // [0, 16) interpolators, [16] position.
  std::array<std::array<uint32_t, 4>, 17> values{};
  std::array<uint32_t, 17> masks{};
  bool killed = false;
  void Export(ucode::ExportRegister export_register, const float* value,
              uint32_t value_mask) override {
    const uint32_t index = uint32_t(export_register);
    size_t slot;
    if (index < 16) {
      slot = index;
    } else if (export_register == ucode::ExportRegister::kVSPosition) {
      slot = 16;
    } else {
      if (export_register == ucode::ExportRegister::kVSPointSizeEdgeFlagKillVertex &&
          (value_mask & 0b0100) &&
          (rex::memory::Reinterpret<uint32_t>(value[2]) & ~(UINT32_C(1) << 31))) {
        killed = true;
      }
      return;
    }
    for (uint32_t c = 0; c < 4; ++c) {
      if (value_mask & (1u << c)) {
        values[slot][c] = rex::memory::Reinterpret<uint32_t>(value[c]);
      }
    }
    masks[slot] |= value_mask;
  }
};
}  // namespace

bool DrawExtentEstimator::HtRectangleExportsConstant(const Shader& vertex_shader,
                                                     uint32_t interpolator_mask) {
  const RegisterFile& regs = register_file_;
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (vgt_draw_initiator.source_select != xenos::SourceSelect::kAutoIndex ||
      vgt_draw_initiator.num_indices != 3 || !vertex_shader.is_ucode_analyzed() ||
      !ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    return false;
  }
  const uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  const uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  const uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  shader_interpreter_.SetShader(vertex_shader);
  HtExportCaptureSink first;
  for (uint32_t i = 0; i < 3; ++i) {
    HtExportCaptureSink sink;
    shader_interpreter_.SetExportSink(&sink);
    const uint32_t vertex_index =
        std::min(max_index, std::max(min_index, (i + index_offset) & 0xFFFFFF));
    shader_interpreter_.temp_registers()[0] = float(vertex_index);
    shader_interpreter_.Execute();
    shader_interpreter_.SetExportSink(nullptr);
    if (sink.killed) {
      return false;
    }
    if (!i) {
      first = sink;
      continue;
    }
    // Position Z and W.
    if ((sink.masks[16] & 0b1100) != (first.masks[16] & 0b1100) ||
        ((sink.masks[16] & 0b0100) && sink.values[16][2] != first.values[16][2]) ||
        ((sink.masks[16] & 0b1000) && sink.values[16][3] != first.values[16][3])) {
      return false;
    }
    for (uint32_t slot = 0; slot < 16; ++slot) {
      if (!(interpolator_mask & (1u << slot))) {
        continue;
      }
      if (sink.masks[slot] != first.masks[slot] || sink.values[slot] != first.values[slot]) {
        return false;
      }
    }
  }
  return true;
}

uint32_t DrawExtentEstimator::EstimateVertexMaxY(const Shader& vertex_shader) {""")
    s = ec.read_text()
    if "#include <array>" not in s:
        s = s.replace("#include <rex/graphics/util/draw_extent_estimator.h>",
                      "#include <rex/graphics/util/draw_extent_estimator.h>\n\n#include <array>", 1)
        ec.write_text(s)

ch = root / "include/rex/graphics/pipeline/render_target/cache.h"
if "HtRectangleExportsConstant" not in ch.read_text():
    patch(ch, "                       uint32_t normalized_color_mask, const Shader& vertex_shader);\n",
          """                       uint32_t normalized_color_mask, const Shader& vertex_shader);
  bool HtRectangleExportsConstant(const Shader& vertex_shader, uint32_t interpolator_mask) {
    return draw_extent_estimator_.HtRectangleExportsConstant(vertex_shader, interpolator_mask);
  }
""")

rc = root / "src/graphics/pipeline/render_target/cache.cpp"
s = rc.read_text()
if "ht_bases" not in s:
    patch(rc, """  bool any = false;
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
  return any;""", """  RenderTargetKey keys[1 + xenos::kMaxColorRenderTargets];
  uint32_t key_count = 0;
  if (normalized_depth_control.z_enable || normalized_depth_control.stencil_enable) {
    auto depth_info = regs.Get<reg::RB_DEPTH_INFO>();
    RenderTargetKey& key = keys[key_count++];
    key.base_tiles = depth_info.depth_base;
    key.is_depth = 1;
    key.resource_format = uint32_t(depth_info.depth_format);
  }
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    if (!(normalized_color_mask & (uint32_t(0b1111) << (4 * i)))) {
      continue;
    }
    auto color_info = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    if (xenos::IsColorRenderTargetFormat64bpp(color_info.color_format)) {
      return false;  // Keep the 32bpp tile math simple.
    }
    RenderTargetKey& key = keys[key_count++];
    key.base_tiles = color_info.color_base;
    key.resource_format = uint32_t(
        GetColorResourceFormat(xenos::GetStorageColorFormat(color_info.color_format)));
  }
  if (!key_count) {
    return false;
  }
  // Like Update: a render target ends where the next bound one begins.
  uint32_t ht_bases[1 + xenos::kMaxColorRenderTargets];
  for (uint32_t i = 0; i < key_count; ++i) {
    ht_bases[i] = keys[i].base_tiles;
  }
  std::sort(ht_bases, ht_bases + key_count);
  for (uint32_t i = 0; i < key_count; ++i) {
    uint32_t next = xenos::kEdramTileCount + ht_bases[0];
    for (uint32_t j = 0; j < key_count; ++j) {
      if (ht_bases[j] > keys[i].base_tiles) {
        next = ht_bases[j];
        break;
      }
    }
    const uint32_t length = std::min(length_tiles, next - keys[i].base_tiles);
    RenderTargetKey key_4x = keys[i];
    key_4x.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    key_4x.msaa_samples = xenos::MsaaSamples::k4X;
    RenderTargetKey key_1x = key_4x;
    key_1x.msaa_samples = xenos::MsaaSamples::k1X;
    if (WouldOwnershipChangeRequireTransfers(key_1x, 0, length) ||
        !WouldOwnershipChangeRequireTransfers(key_4x, 0, length)) {
      return false;
    }
  }
  return true;""")

cp = root / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
if "PA_SC_WINDOW_OFFSET] == 0" not in s:
    patch(cp, "        !mode_cntl.vtx_window_offset_enable && !mode_cntl.poly_offset_front_enable &&",
          "        (!mode_cntl.vtx_window_offset_enable || regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET] == 0) &&\n"
          "        !mode_cntl.poly_offset_front_enable &&")
    patch(cp, """        (!pixel_shader || (!interpolator_mask && ps_param_gen_pos == UINT32_MAX &&""",
          """        (!pixel_shader || (ps_param_gen_pos == UINT32_MAX &&""")
    patch(cp, """        render_target_cache_->HtClearCanUse1x(normalized_depth_control, ht_color_mask,
                                              *vertex_shader)) {
      ht_restore.file = register_file_;""",
          """        render_target_cache_->HtRectangleExportsConstant(*vertex_shader, interpolator_mask) &&
        render_target_cache_->HtClearCanUse1x(normalized_depth_control, ht_color_mask,
                                              *vertex_shader)) {
      ht_restore.file = register_file_;""")
print("patched")
