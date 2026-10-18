"""One-off diagnostics: HT_TRACE_FRAME=<n> logs one frame of EDRAM usage
([ht-trace]): render target binding changes, ownership transfers, resolves.
Requires ht_perf_log=true (the frame counter lives in PerfOnSwap).

usage: python3 patch_rt_trace.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new):
    s = path.read_text()
    assert old in s, (path, old[:80])
    path.write_text(s.replace(old, new, 1))


(root / "include/rex/graphics/ht_trace.h").write_text("""#pragma once
// Hydro Thunder diagnostics: one-frame EDRAM trace (HT_TRACE_FRAME=<n>).
#include <atomic>
#include <cstdint>
namespace rex::graphics {
inline std::atomic<uint32_t> ht_trace_active{0};
inline std::atomic<uint64_t> ht_trace_draw{0};
}  // namespace rex::graphics
""")

cp = root / "src/graphics/vulkan/command_processor.cpp"
if "ht_trace_active" not in cp.read_text():
    patch(cp, "#include <rex/graphics/vulkan/command_processor.h>",
          "#include <rex/graphics/vulkan/command_processor.h>\n#include <rex/graphics/ht_trace.h>")
    patch(cp, """  perf_frame_ms_max_ = std::max(perf_frame_ms_max_, frame_ms);
  ++perf_frames_;""", """  perf_frame_ms_max_ = std::max(perf_frame_ms_max_, frame_ms);
  ++perf_frames_;
  {
    static const uint64_t ht_trace_frame = [] {
      const char* v = std::getenv("HT_TRACE_FRAME");
      return v ? std::strtoull(v, nullptr, 10) : UINT64_C(0);
    }();
    static uint64_t ht_swaps = 0;
    ++ht_swaps;
    if (ht_trace_frame && ht_swaps == ht_trace_frame) {
      ht_trace_draw = 0;
      ht_trace_active = 1;
      REXGPU_WARN("[ht-trace] ===== frame {} begin =====", ht_swaps);
    } else if (ht_trace_active) {
      ht_trace_active = 0;
      REXGPU_WARN("[ht-trace] ===== frame end, draws={} =====", uint64_t(ht_trace_draw));
    }
  }""")

rc = root / "src/graphics/pipeline/render_target/cache.cpp"
if "ht_trace_active" not in rc.read_text():
    s = rc.read_text()
    first_include = s.index("#include <rex/")
    rc.write_text(s[:first_include] + "#include <rex/graphics/ht_trace.h>\n#include <string>\n" + s[first_include:])
    patch(rc, """  if (interlock_barrier_only) {
    // No copying transfers or render target bindings - only needed the barrier.
    return true;
  }
""", """  if (interlock_barrier_only) {
    // No copying transfers or render target bindings - only needed the barrier.
    return true;
  }

  if (ht_trace_active) {
    ++ht_trace_draw;
    static std::string ht_last;
    std::string bound;
    bool any_transfer = false;
    for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
      const uint32_t b = edram_bases_sorted[i].second;
      const RenderTargetKey k = rt_keys[b];
      bound += fmt::format(" {}:{}@{} p{} {}x len{}", k.is_depth ? "D" : "C", k.GetFormatName(),
                           uint32_t(k.base_tiles), uint32_t(k.pitch_tiles_at_32bpp),
                           1u << uint32_t(k.msaa_samples), rt_lengths_tiles[i]);
      any_transfer |= !last_update_transfers_[b].empty();
    }
    if (bound != ht_last || any_transfer) {
      REXGPU_WARN("[ht-trace] draw {} bind{} h={}", uint64_t(ht_trace_draw), bound, height_used);
      ht_last = bound;
    }
    for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
      const uint32_t b = edram_bases_sorted[i].second;
      for (const Transfer& t : last_update_transfers_[b]) {
        const RenderTargetKey s = t.source ? t.source->key() : RenderTargetKey();
        REXGPU_WARN("[ht-trace]   xfer -> {} tiles {}-{} from {}:{}@{} p{} {}x{}",
                    rt_keys[b].is_depth ? "D" : "C", t.start_tiles, t.end_tiles,
                    s.is_depth ? "D" : "C", t.source ? s.GetFormatName() : "?",
                    uint32_t(s.base_tiles), uint32_t(s.pitch_tiles_at_32bpp),
                    1u << uint32_t(s.msaa_samples), t.host_depth_source ? " +hostdepth" : "");
      }
    }
  }
""")

vr = root / "src/graphics/vulkan/render_target_cache.cpp"
if "[ht-trace] resolve" not in vr.read_text():
    s = vr.read_text()
    if "rex/graphics/ht_trace.h" not in s:
        patch(vr, "#include <rex/logging.h>\n", "#include <rex/logging.h>\n#include <rex/graphics/ht_trace.h>\n")
    patch(vr, """  // Nothing to copy/clear.
  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {""",
          """  if (ht_trace_active) {
    REXGPU_WARN(
        "[ht-trace] resolve after draw {} src={} depth_clear={} color_clear={} depth_base={} "
        "color_base={} w8={} h8={} copy_len={} dest=0x{:08X}",
        uint64_t(ht_trace_draw), uint32_t(resolve_info.rb_copy_control.copy_src_select),
        uint32_t(resolve_info.rb_copy_control.depth_clear_enable),
        uint32_t(resolve_info.rb_copy_control.color_clear_enable), resolve_info.depth_original_base,
        resolve_info.color_original_base, uint32_t(resolve_info.coordinate_info.width_div_8),
        resolve_info.height_div_8, resolve_info.copy_dest_extent_length, resolve_info.copy_dest_base);
  }
  // Nothing to copy/clear.
  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {""")
print("patched")
