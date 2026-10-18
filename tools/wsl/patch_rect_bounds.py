"""Adds DrawExtentEstimator::HtRectangleBounds (guest-pixel bounds of an
auto-indexed 3-vertex screen-space rectangle, after the VTE scale/offset and
the D3D half-pixel offset) and logs it in the [ht-trace] bind lines.

usage: python3 patch_rect_bounds.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new):
    s = path.read_text()
    assert old in s, (path, old[:90])
    path.write_text(s.replace(old, new, 1))


eh = root / "include/rex/graphics/util/draw_extent_estimator.h"
if "HtRectangleBounds" not in eh.read_text():
    patch(eh, "  bool HtRectangleExportsConstant(const Shader& vertex_shader, uint32_t interpolator_mask);",
          """  bool HtRectangleExportsConstant(const Shader& vertex_shader, uint32_t interpolator_mask);
  // ht: bounds in guest pixels (x0, y0, x1, y1) of an auto-indexed 3-vertex
  // rectangle with screen-space positions, including the half-pixel offset.
  bool HtRectangleBounds(const Shader& vertex_shader, float bounds_out[4]);""")

ec = root / "src/graphics/util/draw_extent_estimator.cpp"
if "DrawExtentEstimator::HtRectangleBounds" not in ec.read_text():
    patch(ec, "uint32_t DrawExtentEstimator::EstimateVertexMaxY(const Shader& vertex_shader) {",
          """bool DrawExtentEstimator::HtRectangleBounds(const Shader& vertex_shader, float bounds_out[4]) {
  const RegisterFile& regs = register_file_;
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  auto vte = regs.Get<reg::PA_CL_VTE_CNTL>();
  if (vgt_draw_initiator.source_select != xenos::SourceSelect::kAutoIndex ||
      vgt_draw_initiator.num_indices != 3 || !vte.vtx_xy_fmt || !vertex_shader.is_ucode_analyzed() ||
      !ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    return false;
  }
  const float scale[2] = {
      vte.vport_x_scale_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XSCALE) : 1.0f,
      vte.vport_y_scale_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE) : 1.0f};
  float offset[2] = {
      vte.vport_x_offset_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XOFFSET) : 0.0f,
      vte.vport_y_offset_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET) : 0.0f};
  if (regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
    auto window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
    offset[0] += float(window_offset.window_x_offset);
    offset[1] += float(window_offset.window_y_offset);
  }
  if (REXCVAR_GET(half_pixel_offset) &&
      regs.Get<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero) {
    offset[0] += 0.5f;
    offset[1] += 0.5f;
  }
  const uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  const uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  const uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  shader_interpreter_.SetShader(vertex_shader);
  float lo[2] = {FLT_MAX, FLT_MAX}, hi[2] = {-FLT_MAX, -FLT_MAX};
  for (uint32_t i = 0; i < 3; ++i) {
    HtExportCaptureSink sink;
    shader_interpreter_.SetExportSink(&sink);
    shader_interpreter_.temp_registers()[0] =
        float(std::min(max_index, std::max(min_index, (i + index_offset) & 0xFFFFFF)));
    shader_interpreter_.Execute();
    shader_interpreter_.SetExportSink(nullptr);
    if (sink.killed || (sink.masks[16] & 0b0011) != 0b0011) {
      return false;
    }
    for (uint32_t a = 0; a < 2; ++a) {
      const float v = rex::memory::Reinterpret<float>(sink.values[16][a]) * scale[a] + offset[a];
      lo[a] = std::min(lo[a], v);
      hi[a] = std::max(hi[a], v);
    }
  }
  bounds_out[0] = lo[0];
  bounds_out[1] = lo[1];
  bounds_out[2] = hi[0];
  bounds_out[3] = hi[1];
  return true;
}

uint32_t DrawExtentEstimator::EstimateVertexMaxY(const Shader& vertex_shader) {""")

ch = root / "include/rex/graphics/pipeline/render_target/cache.h"
if "HtRectangleBounds" not in ch.read_text():
    patch(ch, "  bool HtRectangleExportsConstant(const Shader& vertex_shader, uint32_t interpolator_mask) {",
          """  bool HtRectangleBounds(const Shader& vertex_shader, float bounds_out[4]) {
    return draw_extent_estimator_.HtRectangleBounds(vertex_shader, bounds_out);
  }
  bool HtRectangleExportsConstant(const Shader& vertex_shader, uint32_t interpolator_mask) {""")

rc = root / "src/graphics/pipeline/render_target/cache.cpp"
if "rect=(" not in rc.read_text():
    patch(rc, """      REXGPU_WARN(
          "[ht-trace] draw {} bind{} h={} | init=0x{:08X}""",
          """      float ht_rect[4] = {};
      const bool ht_has_rect = draw_extent_estimator_.HtRectangleBounds(vertex_shader, ht_rect);
      if (ht_has_rect) {
        REXGPU_WARN("[ht-trace]   rect=({},{})-({},{}) pitch_px={}", ht_rect[0], ht_rect[1],
                    ht_rect[2], ht_rect[3], pitch_pixels);
      }
      REXGPU_WARN(
          "[ht-trace] draw {} bind{} h={} | init=0x{:08X}""")
print("patched")
