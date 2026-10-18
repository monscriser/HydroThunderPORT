"""MEASUREMENT ONLY (diagnostic kit): HT_TRACE_FRAME accepts a comma list of swap
numbers ("12000,15000,18000"), so a player who cannot know when the race starts still
gets traced frames inside it. Each traced frame is delimited by the usual
"[ht-trace] ===== frame N begin =====" / "frame end" lines.
usage: python3 -I patch_trace_multi.py /root/ht/sdk
"""
import sys
from pathlib import Path

p = Path(sys.argv[1]) / 'src/graphics/vulkan/command_processor.cpp'
s = p.read_text()
old = '''    static const uint64_t ht_trace_frame = [] {
      const char* v = std::getenv("HT_TRACE_FRAME");
      return v ? std::strtoull(v, nullptr, 10) : UINT64_C(0);
    }();
    static uint64_t ht_swaps = 0;
    ++ht_swaps;
    if (ht_trace_frame && ht_swaps == ht_trace_frame) {'''
new = '''    static const std::vector<uint64_t> ht_trace_frames = [] {
      std::vector<uint64_t> frames;
      if (const char* v = std::getenv("HT_TRACE_FRAME")) {
        for (const char* c = v; *c;) {
          char* end = nullptr;
          const uint64_t n = std::strtoull(c, &end, 10);
          if (end == c) break;
          if (n) frames.push_back(n);
          c = (*end == ',') ? end + 1 : end;
        }
      }
      return frames;
    }();
    static uint64_t ht_swaps = 0;
    ++ht_swaps;
    if (std::find(ht_trace_frames.begin(), ht_trace_frames.end(), ht_swaps) != ht_trace_frames.end()) {'''
assert s.count(old) == 1
p.write_text(s.replace(old, new))
print('patched HT_TRACE_FRAME list (measurement only)')
