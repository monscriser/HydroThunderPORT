"""One-off: adds ht_perf_log diagnostics that list which EDRAM transfers and
texture loads the game causes ([ht-xfer] / [ht-texload], every 10 s).

usage: python3 patch_gpu_diag.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])

rt = root / "src/graphics/vulkan/render_target_cache.cpp"
s = rt.read_text()
if "[ht-xfer]" not in s:
    s = s.replace("#include <vector>\n", "#include <vector>\n#include <chrono>\n#include <map>\n#include <string>\n", 1)
    s = s.replace("#include <rex/logging.h>\n", "#include <rex/logging.h>\n#include <rex/ui/flags.h>\n", 1)
    anchor = "  ++command_processor_.perf_counters().transfers;\n"
    assert anchor in s
    s = s.replace(anchor, anchor + r'''  // ht diagnostics: which EDRAM ownership transfers the game causes.
  if (REXCVAR_GET(ht_perf_log)) {
    static std::map<std::string, std::pair<uint64_t, uint64_t>> ht_stats;  // count, tiles
    static auto ht_t0 = std::chrono::steady_clock::now();
    if (resolve_clear_needed) ++ht_stats["resolve-clear"].first;
    for (uint32_t i = 0; i < render_target_count; ++i) {
      if (!render_targets[i] || !render_target_transfers) continue;
      const RenderTargetKey dk = render_targets[i]->key();
      for (const Transfer& t : render_target_transfers[i]) {
        std::string name = "?";
        if (t.source) {
          const RenderTargetKey sk = t.source->key();
          name = fmt::format("{}:{} {}x pitch{} -> {}:{} {}x pitch{}", sk.is_depth ? "D" : "C",
                             sk.GetFormatName(), 1u << uint32_t(sk.msaa_samples),
                             uint32_t(sk.pitch_tiles_at_32bpp), dk.is_depth ? "D" : "C",
                             dk.GetFormatName(), 1u << uint32_t(dk.msaa_samples),
                             uint32_t(dk.pitch_tiles_at_32bpp));
        }
        if (t.host_depth_source) name += " +hostdepth";
        auto& e = ht_stats[name];
        ++e.first;
        e.second += t.end_tiles - t.start_tiles;
      }
    }
    const auto ht_now = std::chrono::steady_clock::now();
    if (ht_now - ht_t0 >= std::chrono::seconds(10)) {
      ht_t0 = ht_now;
      for (const auto& [k, v] : ht_stats) REXGPU_WARN("[ht-xfer] {} count={} tiles={}", k, v.first, v.second);
      ht_stats.clear();
    }
  }
''', 1)
    rt.write_text(s)

tc = root / "src/graphics/vulkan/texture_cache.cpp"
s = tc.read_text()
if "[ht-texload]" not in s:
    if "#include <rex/ui/flags.h>" not in s:
        s = s.replace("#include <rex/logging.h>\n", "#include <rex/logging.h>\n#include <rex/ui/flags.h>\n", 1)
    if "#include <map>" not in s:
        s = s.replace("#include <algorithm>\n", "#include <algorithm>\n#include <chrono>\n#include <map>\n#include <string>\n", 1)
    anchor = "  ++command_processor_.perf_counters().texture_loads;\n"
    assert anchor in s
    s = s.replace(anchor, anchor + r'''  // ht diagnostics: which textures are (re)loaded, e.g. render-to-texture.
  if (REXCVAR_GET(ht_perf_log)) {
    static std::map<std::string, uint64_t> ht_stats;
    static auto ht_t0 = std::chrono::steady_clock::now();
    ++ht_stats[fmt::format("fmt{} {}x{}x{} dim{}{}{}{}", uint32_t(texture_key.format),
                           texture_key.GetWidth(), texture_key.GetHeight(),
                           texture_key.GetDepthOrArraySize(), uint32_t(texture_key.dimension),
                           texture_key.scaled_resolve ? " scaled" : "", load_base ? " base" : "",
                           load_mips ? " mips" : "")];
    const auto ht_now = std::chrono::steady_clock::now();
    if (ht_now - ht_t0 >= std::chrono::seconds(10)) {
      ht_t0 = ht_now;
      for (const auto& [k, v] : ht_stats) REXGPU_WARN("[ht-texload] {} count={}", k, v);
      ht_stats.clear();
    }
  }
''', 1)
    tc.write_text(s)
print("patched")
