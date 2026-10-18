"""MEASUREMENT ONLY (revert after): time spent in SharedMemory::RequestRanges.

Adds rdtsc ticks / call count / calls that needed an upload, accumulated in globals
(command processor thread only), printed next to [ht-draw] as
  [ht-reqrange] ms/frame=... calls/frame=... upload_calls/frame=...
usage: python3 -I patch_measure_reqrange.py /root/ht/sdk
"""
import sys
from pathlib import Path

sdk = Path(sys.argv[1])

def edit(rel, old, new, count=1):
    p = sdk / rel
    s = p.read_text()
    assert s.count(old) == count, (rel, old[:60], s.count(old))
    p.write_text(s.replace(old, new))

sm = 'src/graphics/shared_memory.cpp'
edit(sm, '#include <utility>\n',
     '#include <utility>\n#include <immintrin.h>\n\n'
     'namespace rex::graphics {\n'
     'uint64_t g_ht_rr_ticks = 0, g_ht_rr_calls = 0, g_ht_rr_upload_calls = 0;\n'
     'namespace {\n'
     'struct HtRrTimer {\n'
     '  uint64_t t = __rdtsc();\n'
     '  ~HtRrTimer() { g_ht_rr_ticks += __rdtsc() - t; ++g_ht_rr_calls; }\n'
     '};\n'
     '}  // namespace\n'
     '}  // namespace rex::graphics\n')
edit(sm, 'bool SharedMemory::RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count) {\n',
     'bool SharedMemory::RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count) {\n'
     '  HtRrTimer ht_rr_timer;\n')
edit(sm, '  bool uploaded = UploadRanges(upload_ranges_);\n',
     '  ++g_ht_rr_upload_calls;\n  bool uploaded = UploadRanges(upload_ranges_);\n')

cp = 'src/graphics/vulkan/command_processor.cpp'
p = sdk / cp
s = p.read_text()
anchor = 'namespace rex::graphics::vulkan {'
assert anchor in s
s = s.replace(anchor, 'namespace rex::graphics {\n'
              'extern uint64_t g_ht_rr_ticks, g_ht_rr_calls, g_ht_rr_upload_calls;\n'
              '}  // namespace rex::graphics\n' + anchor, 1)
p.write_text(s)
edit(cp, '      perf_cpu_ticks_.fill(0);\n',
     '      {\n'
     '        REXGPU_WARN("[ht-reqrange] ms/frame={:.3f} calls/frame={:.0f} upload_calls/frame={:.1f}",\n'
     '                    double(g_ht_rr_ticks) * ns_per_tick / 1e6 / f, double(g_ht_rr_calls) / f,\n'
     '                    double(g_ht_rr_upload_calls) / f);\n'
     '        g_ht_rr_ticks = g_ht_rr_calls = g_ht_rr_upload_calls = 0;\n'
     '      }\n'
     '      perf_cpu_ticks_.fill(0);\n')
print('patched (measurement only)')
