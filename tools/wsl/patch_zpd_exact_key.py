"""ht_zpd_exact_key: occlusion (ZPD) estimates per full report address.

Lights showing through walls (Asgard start, Ian 16/10/2026). Each light draws a test
sphere twice (depth off = total samples, depth on = visible samples), once per predicated
tiling half; every half/bank writes its reports at a different 4 KB page (0E9DF..,
0E9E0.., 0E9E1.., 0E9E2..). The asynchronous ZPD emulation answers each report with the
last measured value of the same interval, but it keyed that value by the report's slot
inside the page (address & 0xFFF), so different lights and different halves sharing a slot
overwrote each other's measurement: a light hidden behind rocks could receive another
light's "visible" count (traced: key 440 measured 82 in one half and 548 in the other).

Now the estimate is looked up by the full report address first; the slot value remains
only as the fallback for an address never measured yet (first frames of a light). Same
one-frame latency and the same running-counter scheme; no extra GPU work.
usage: python3 -I patch_zpd_exact_key.py /root/ht/sdk
"""
import sys
from pathlib import Path

sdk = Path(sys.argv[1])

def edit(rel, old, new):
    p = sdk / rel
    s = p.read_text()
    assert s.count(old) == 1, (rel, old[:70])
    p.write_text(s.replace(old, new))

edit('include/rex/graphics/vulkan/command_processor.h',
     '  std::unordered_map<uint32_t, uint64_t> zpd_interval_estimate_;\n',
     '  std::unordered_map<uint32_t, uint64_t> zpd_interval_estimate_;\n'
     '  // ht_zpd_exact_key: last measurement per full report address.\n'
     '  std::unordered_map<uint32_t, uint64_t> ht_zpd_address_estimate_;\n')

cp = 'src/graphics/vulkan/command_processor.cpp'
edit(cp, 'REXCVAR_DEFINE_BOOL(ht_zpd_flush, true, "GPU/Vulkan",\n',
     'REXCVAR_DEFINE_BOOL(ht_zpd_exact_key, true, "GPU/Vulkan",\n'
     '                    "Occlusion estimates per full report address (lights no longer borrow another '
     'light\'s or tiling half\'s visibility)");\n'
     'REXCVAR_DEFINE_BOOL(ht_zpd_flush, true, "GPU/Vulkan",\n')
# With exact keys the estimate no longer depends on getting the results back one frame
# sooner: measured on a moving Asgard race, leaks 169/200000 with the per-query submission
# and 156/268000 without it, while the submissions drop from ~9-11 to 3 per frame and the
# command processor CPU per draw by ~5%. So the extra submission is off by default.
edit(cp, 'REXCVAR_DEFINE_BOOL(ht_zpd_flush, true, "GPU/Vulkan",\n',
     'REXCVAR_DEFINE_BOOL(ht_zpd_flush, false, "GPU/Vulkan",\n')
edit(cp, '    ZpdCloseInterval(report_address & 0xFFF);\n',
     '    ZpdCloseInterval(REXCVAR_GET(ht_zpd_exact_key) ? report_address : (report_address & 0xFFF));\n')
edit(cp, '''  if (key != UINT32_MAX) {
    auto it = zpd_interval_estimate_.find(key);
    if (it != zpd_interval_estimate_.end()) {
      estimate = it->second;
    }
  }''', '''  if (key != UINT32_MAX) {
    auto exact = ht_zpd_address_estimate_.end();
    if (key > 0xFFF) {
      exact = ht_zpd_address_estimate_.find(key);
    }
    if (exact != ht_zpd_address_estimate_.end()) {
      estimate = exact->second;
    } else {
      auto it = zpd_interval_estimate_.find(key & 0xFFF);
      if (it != zpd_interval_estimate_.end()) {
        estimate = it->second;
      }
    }
  }''')
edit(cp, '''    if (interval.key != UINT32_MAX) {
      zpd_interval_estimate_[interval.key] = samples;
    }''', '''    if (interval.key != UINT32_MAX) {
      zpd_interval_estimate_[interval.key & 0xFFF] = samples;
      if (interval.key > 0xFFF) {
        ht_zpd_address_estimate_[interval.key] = samples;
      }
    }''')
print('patched ht_zpd_exact_key')
