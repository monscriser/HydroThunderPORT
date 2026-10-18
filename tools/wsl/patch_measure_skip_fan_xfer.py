"""Measurement only (coverage NOT proven, may break the image): HT_MEASURE_SKIP_FAN_XFER=1
drops the color ownership transfers before a draw that looks like a full-screen pass
(auto-indexed 4-vertex triangle fan, color write mask 0xF, blend ONE/ZERO), to
measure what a proven version could save. Counts in [ht-fanskip].

usage: python3 patch_measure_skip_fan_xfer.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])
f = root / "src/graphics/pipeline/render_target/cache.cpp"
s = f.read_text()
old = """  if (REXCVAR_GET(ht_skip_clear_color_pretransfer)) {"""
new = """  {
    static const bool ht_measure_fan = std::getenv("HT_MEASURE_SKIP_FAN_XFER") != nullptr;
    const uint32_t initiator = regs[XE_GPU_REG_VGT_DRAW_INITIATOR];
    if (ht_measure_fan && (initiator & 0x3F) == uint32_t(xenos::PrimitiveType::kTriangleFan) &&
        ((initiator >> 6) & 0x3) == 0x2 && (initiator >> 16) == 4) {
      static uint64_t ht_fan_skips = 0, ht_fan_tiles = 0, ht_fan_calls = 0;
      for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
        const uint32_t b = edram_bases_sorted[i].second;
        if (b == 0 || last_update_transfers_[b].empty()) continue;
        const uint32_t color_index = b - 1;
        const uint32_t write_mask = (normalized_color_mask >> (color_index * 4)) & 0xF;
        const uint32_t blend = regs[reg::RB_BLENDCONTROL::rt_register_indices[color_index]];
        if (write_mask == 0xF && (blend & 0x1FFF) == 0x0001 && ((blend >> 16) & 0x1FFF) == 0x0001) {
          for (const Transfer& t : last_update_transfers_[b]) ht_fan_tiles += t.end_tiles - t.start_tiles;
          last_update_transfers_[b].clear();
          ++ht_fan_skips;
        }
      }
      if (++ht_fan_calls % 600 == 0) {
        REXGPU_WARN("[ht-fanskip] fan draws={} skipped={} tiles={}", ht_fan_calls, ht_fan_skips,
                    ht_fan_tiles);
      }
    }
  }
  if (REXCVAR_GET(ht_skip_clear_color_pretransfer)) {"""
assert s.count(old) == 1
s = s.replace(old, new)
if "#include <cstdlib>" not in s:
    s = "#include <cstdlib>\n" + s
f.write_text(s)
print("patched measure skip fan xfer")
