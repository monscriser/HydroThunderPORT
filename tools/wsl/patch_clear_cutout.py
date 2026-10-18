"""Clear-rectangle cutout for EDRAM ownership transfers (Hydro Thunder).

Frame trace: before every shadow map, reflection and depth buffer the game
clears the render target with a screen-space rectangle (split into a narrow
right strip and the rest: rectdraw 368 (960,0)-(1024,1024) + 369 (0,0)-(960,1024)).
The engine still copied the previous EDRAM owner's data into the whole target
first (up to 1664 tiles, 4x MSAA sources). Now the transfers skip the area the
clear overwrites, using the engine's own transfer cutout (the one resolve clears
use); a right strip waits one draw so the two halves are cut out together.
Pixels outside the clear (pitch padding) are still copied, so the result is the
same as before except stencil under a depth-only clear (never tested by the
game: stencil is only enabled in its clears).
Cvar ht_clear_transfer_cutout (default true).

usage: python3 patch_clear_cutout.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new, count=1):
    s = path.read_text()
    assert s.count(old) == count, (path, s.count(old), old[:100])
    path.write_text(s.replace(old, new))


base_h = root / "include/rex/graphics/pipeline/render_target/cache.h"
if "ht_last_update_transfers_mutable" not in base_h.read_text():
    patch(base_h, "  bool HtRectangleBounds(const Shader& vertex_shader, float bounds_out[4]) {",
          "  std::vector<Transfer>* ht_last_update_transfers_mutable() { return last_update_transfers_; }\n"
          "  bool HtRectangleBounds(const Shader& vertex_shader, float bounds_out[4]) {")

vk_h = root / "include/rex/graphics/vulkan/render_target_cache.h"
if "ht_clear_ps_ok_" not in vk_h.read_text():
    patch(vk_h, "  void ClearCache() override;\n",
          """  void ClearCache() override;

  // ht_clear_transfer_cutout: set by the command processor before Update
  // (pixel shader does not kill pixels or write depth).
  bool ht_clear_ps_ok_ = false;
""")
    patch(vk_h, "\n private:\n", """
 private:
  // ht_clear_transfer_cutout.
  bool HtClearSlots(reg::RB_DEPTHCONTROL normalized_depth_control, uint32_t normalized_color_mask,
                    const Shader& vertex_shader, RenderTarget* const* rts, uint32_t& slots_out,
                    Transfer::Rectangle& rect_out);
  void HtClearCutoutTransfers(RenderTarget* const* rts,
                              reg::RB_DEPTHCONTROL normalized_depth_control,
                              uint32_t normalized_color_mask, const Shader& vertex_shader);
  void HtPerformCutTransfers(RenderTarget* const* rts, std::vector<Transfer>* transfers,
                             const Transfer::Rectangle& cut);
  void HtFlushPendingClear();
  bool ht_pending_active_ = false;
  uint32_t ht_pending_slots_ = 0;
  RenderTarget* ht_pending_rts_[1 + xenos::kMaxColorRenderTargets] = {};
  std::vector<Transfer> ht_pending_transfers_[1 + xenos::kMaxColorRenderTargets];
  Transfer::Rectangle ht_pending_rect_ = {};
  uint64_t ht_cut_stats_[4] = {};  // joined pair, strip alone, single clear, tiles saved

""", count=1)

cp = root / "src/graphics/vulkan/command_processor.cpp"
if "ht_clear_ps_ok_" not in cp.read_text():
    patch(cp, """  if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                    normalized_color_mask, *vertex_shader)) {""",
          """  render_target_cache_->ht_clear_ps_ok_ =
      !pixel_shader || (!pixel_shader->kills_pixels() && !pixel_shader->writes_depth());
  if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                    normalized_color_mask, *vertex_shader)) {""")

vk = root / "src/graphics/vulkan/render_target_cache.cpp"
if "HtClearCutoutTransfers" not in vk.read_text():
    patch(vk, "#include <rex/graphics/ht_trace.h>\n",
          "#include <rex/graphics/ht_trace.h>\n#include <cmath>\n\n"
          'REXCVAR_DEFINE_BOOL(ht_clear_transfer_cutout, true, "GPU",\n'
          '                    "Do not copy old EDRAM data into the area a clear rectangle overwrites");\n')
    patch(vk, """      PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                       depth_and_color_render_targets, last_update_transfers());
""", """      HtClearCutoutTransfers(depth_and_color_render_targets, normalized_depth_control,
                             normalized_color_mask, vertex_shader);
      PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                       depth_and_color_render_targets, last_update_transfers());
""")
    patch(vk, """  written_address_out = 0;
  written_length_out = 0;

  bool draw_resolution_scaled = IsDrawResolutionScaled();
""", """  written_address_out = 0;
  written_length_out = 0;

  HtFlushPendingClear();

  bool draw_resolution_scaled = IsDrawResolutionScaled();
""")
    patch(vk, """void VulkanRenderTargetCache::ClearCache() {
""", """void VulkanRenderTargetCache::ClearCache() {
  ht_pending_active_ = false;
  for (auto& transfers : ht_pending_transfers_) transfers.clear();
""")
    s = vk.read_text()
    marker = "void VulkanRenderTargetCache::ClearCache() {"
    s = s.replace(marker, r'''bool VulkanRenderTargetCache::HtClearSlots(reg::RB_DEPTHCONTROL normalized_depth_control,
                                           uint32_t normalized_color_mask,
                                           const Shader& vertex_shader, RenderTarget* const* rts,
                                           uint32_t& slots_out, Transfer::Rectangle& rect_out) {
  const RegisterFile& regs = register_file();
  const uint32_t initiator = regs[XE_GPU_REG_VGT_DRAW_INITIATOR];
  // Auto-indexed 3-vertex rectangle in screen space (no viewport transform).
  if ((initiator & 0x3F) != uint32_t(xenos::PrimitiveType::kRectangleList) ||
      ((initiator >> 6) & 0x3) != 0x2 || (initiator >> 16) != 3 || !ht_clear_ps_ok_) {
    return false;
  }
  auto vte = regs.Get<reg::PA_CL_VTE_CNTL>();
  auto colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  auto mode = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  if (!vte.vtx_xy_fmt || colorcontrol.alpha_to_mask_enable ||
      (colorcontrol.alpha_test_enable &&
       colorcontrol.alpha_func != xenos::CompareFunction::kAlways) ||
      mode.cull_front || mode.cull_back || (regs[XE_GPU_REG_PA_SC_AA_MASK] & 0xF) != 0xF ||
      regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET] != 0) {
    return false;
  }
  uint32_t slots = 0;
  if (rts[0] && normalized_depth_control.z_enable && normalized_depth_control.z_write_enable &&
      normalized_depth_control.zfunc == xenos::CompareFunction::kAlways) {
    slots |= 1;
  }
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    const uint32_t blend = regs[reg::RB_BLENDCONTROL::rt_register_indices[i]];
    // All channels written with ONE * source + ZERO * destination.
    if (rts[1 + i] && ((normalized_color_mask >> (i * 4)) & 0xF) == 0xF &&
        (blend & 0x1FFF) == 0x0001 && ((blend >> 16) & 0x1FFF) == 0x0001) {
      slots |= 1u << (1 + i);
    }
  }
  float r[4];
  if (!slots || !HtRectangleBounds(vertex_shader, r)) {
    return false;
  }
  // Pixels whose centers are inside, limited by the window and screen scissors.
  auto wtl = regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>();
  auto wbr = regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
  auto stl = regs.Get<reg::PA_SC_SCREEN_SCISSOR_TL>();
  auto sbr = regs.Get<reg::PA_SC_SCREEN_SCISSOR_BR>();
  int64_t x0 = int64_t(std::ceil(double(r[0]) - 0.5)), y0 = int64_t(std::ceil(double(r[1]) - 0.5));
  int64_t x1 = int64_t(std::ceil(double(r[2]) - 0.5)), y1 = int64_t(std::ceil(double(r[3]) - 0.5));
  x0 = std::max({x0, int64_t(0), int64_t(wtl.tl_x), int64_t(stl.tl_x)});
  y0 = std::max({y0, int64_t(0), int64_t(wtl.tl_y), int64_t(stl.tl_y)});
  x1 = std::min({x1, int64_t(wbr.br_x), int64_t(sbr.br_x), int64_t(8192)});
  y1 = std::min({y1, int64_t(wbr.br_y), int64_t(sbr.br_y), int64_t(8192)});
  if (x1 <= x0 || y1 <= y0) {
    return false;
  }
  rect_out.x_pixels = uint32_t(x0);
  rect_out.y_pixels = uint32_t(y0);
  rect_out.width_pixels = uint32_t(x1 - x0);
  rect_out.height_pixels = uint32_t(y1 - y0);
  slots_out = slots;
  return true;
}

void VulkanRenderTargetCache::HtPerformCutTransfers(RenderTarget* const* rts,
                                                    std::vector<Transfer>* transfers,
                                                    const Transfer::Rectangle& cut) {
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    if (!rts[i]) {
      transfers[i].clear();
      continue;
    }
    const RenderTargetKey key = rts[i]->key();
    auto& list = transfers[i];
    const size_t before = list.size();
    for (const Transfer& t : list) ht_cut_stats_[3] += t.end_tiles - t.start_tiles;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Transfer& t) {
                                Transfer::Rectangle rects[Transfer::kMaxRectanglesWithCutout];
                                return t.GetRectangles(key.base_tiles, key.pitch_tiles_at_32bpp,
                                                       key.msaa_samples, key.Is64bpp(), rects,
                                                       &cut) == 0;
                              }),
               list.end());
    (void)before;
  }
  PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, rts, transfers, nullptr,
                                   &cut);
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) transfers[i].clear();
}

void VulkanRenderTargetCache::HtFlushPendingClear() {
  if (!ht_pending_active_) {
    return;
  }
  ht_pending_active_ = false;
  ++ht_cut_stats_[1];
  HtPerformCutTransfers(ht_pending_rts_, ht_pending_transfers_, ht_pending_rect_);
}

void VulkanRenderTargetCache::HtClearCutoutTransfers(RenderTarget* const* rts,
                                                     reg::RB_DEPTHCONTROL normalized_depth_control,
                                                     uint32_t normalized_color_mask,
                                                     const Shader& vertex_shader) {
  if (!REXCVAR_GET(ht_clear_transfer_cutout)) {
    return;
  }
  std::vector<Transfer>* transfers = ht_last_update_transfers_mutable();
  uint32_t slots = 0;
  Transfer::Rectangle rect = {};
  const bool is_clear = HtClearSlots(normalized_depth_control, normalized_color_mask,
                                     vertex_shader, rts, slots, rect);
  if (ht_pending_active_) {
    // Second half of a split clear: same targets, same rows, touching columns.
    const Transfer::Rectangle& p = ht_pending_rect_;
    bool same = is_clear && slots == ht_pending_slots_;
    for (uint32_t i = 0; same && i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      same = !(slots & (1u << i)) || rts[i] == ht_pending_rts_[i];
    }
    if (same && rect.y_pixels <= p.y_pixels &&
        rect.y_pixels + rect.height_pixels >= p.y_pixels + p.height_pixels &&
        rect.x_pixels <= p.x_pixels + p.width_pixels &&
        rect.x_pixels + rect.width_pixels >= p.x_pixels) {
      const uint32_t x0 = std::min(rect.x_pixels, p.x_pixels);
      const uint32_t x1 =
          std::max(rect.x_pixels + rect.width_pixels, p.x_pixels + p.width_pixels);
      ht_pending_rect_.x_pixels = x0;
      ht_pending_rect_.width_pixels = x1 - x0;
      ht_pending_active_ = false;
      ++ht_cut_stats_[0];
      HtPerformCutTransfers(ht_pending_rts_, ht_pending_transfers_, ht_pending_rect_);
      for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
        if (slots & (1u << i)) transfers[i].clear();  // already the pending targets
      }
      return;
    }
    HtFlushPendingClear();
  }
  if (!is_clear) {
    return;
  }
  bool any = false;
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    any |= (slots & (1u << i)) && !transfers[i].empty();
  }
  if (!any) {
    return;
  }
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    ht_pending_transfers_[i].clear();
    ht_pending_rts_[i] = nullptr;
    if (slots & (1u << i)) {
      ht_pending_rts_[i] = rts[i];
      ht_pending_transfers_[i].swap(transfers[i]);
    }
  }
  ht_pending_slots_ = slots;
  ht_pending_rect_ = rect;
  if (rect.x_pixels == 0) {
    // Starts at the left edge: nothing to join, cut out now.
    ++ht_cut_stats_[2];
    HtPerformCutTransfers(ht_pending_rts_, ht_pending_transfers_, ht_pending_rect_);
  } else {
    ht_pending_active_ = true;
  }
  static auto ht_t0 = std::chrono::steady_clock::now();
  const auto ht_now = std::chrono::steady_clock::now();
  if (ht_now - ht_t0 >= std::chrono::seconds(10)) {
    ht_t0 = ht_now;
    REXGPU_WARN("[ht-cutout] joined={} strip_alone={} single={} tiles_in={}", ht_cut_stats_[0],
                ht_cut_stats_[1], ht_cut_stats_[2], ht_cut_stats_[3]);
  }
}

''' + marker, 1)
    vk.write_text(s)
print("patched")
