"""Sentinel v2: fill only the part of the cut area the game has not drawn yet
(for a split clear: the second half; for a single clear: its rectangle; nothing
for a strip flushed alone). v1 also overwrote the already drawn strip, which is
kept as the positive control (sentinela-controle-positivo: garbage visible).
usage: python3 patch_clear_cutout_sentinel2.py /root/ht/sdk"""
import sys
from pathlib import Path
vk = Path(sys.argv[1]) / "src/graphics/vulkan/render_target_cache.cpp"
s = vk.read_text()
if "ht_fill_valid" not in s:
    old = """  PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, rts, transfers,
                                   REXCVAR_GET(ht_clear_cutout_sentinel) ? ht_sentinel : nullptr,
                                   &cut);
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) transfers[i].clear();"""
    assert old in s
    s = s.replace(old, """  PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, rts, transfers, nullptr,
                                   &cut);
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) transfers[i].clear();
  if (REXCVAR_GET(ht_clear_cutout_sentinel) && ht_fill_valid) {
    Transfer::Rectangle fill = {ht_fill[0], ht_fill[1], ht_fill[2], ht_fill[3]};
    PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, rts, transfers,
                                     ht_sentinel, &fill);
  }
  ht_fill_valid = false;""", 1)
    old = "void VulkanRenderTargetCache::HtPerformCutTransfers("
    s = s.replace(old, "static bool ht_fill_valid = false;\nstatic uint32_t ht_fill[4] = {};\n\n" + old, 1)
    # Joined: the second half (this draw's rectangle) is not drawn yet.
    old = """      ++ht_cut_stats_[0];
      HtPerformCutTransfers(ht_pending_rts_, ht_pending_transfers_, ht_pending_rect_);"""
    assert old in s
    s = s.replace(old, """      ++ht_cut_stats_[0];
      ht_fill_valid = true;
      ht_fill[0] = rect.x_pixels; ht_fill[1] = rect.y_pixels;
      ht_fill[2] = rect.width_pixels; ht_fill[3] = rect.height_pixels;
      HtPerformCutTransfers(ht_pending_rts_, ht_pending_transfers_, ht_pending_rect_);""", 1)
    old = """    ++ht_cut_stats_[2];
    HtPerformCutTransfers(ht_pending_rts_, ht_pending_transfers_, ht_pending_rect_);"""
    assert old in s
    s = s.replace(old, """    ++ht_cut_stats_[2];
    ht_fill_valid = true;
    ht_fill[0] = rect.x_pixels; ht_fill[1] = rect.y_pixels;
    ht_fill[2] = rect.width_pixels; ht_fill[3] = rect.height_pixels;
    HtPerformCutTransfers(ht_pending_rts_, ht_pending_transfers_, ht_pending_rect_);""", 1)
    vk.write_text(s)
print("patched")
