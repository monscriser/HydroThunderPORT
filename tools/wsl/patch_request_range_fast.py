"""ht_request_range_fast: single-range SharedMemory::RequestRanges fast path.

Measured (race, 1080p Qualidade, 60 FPS, patch_measure_reqrange.py): ~2000 calls/frame,
~0.43 ms/frame on the command processor thread; only ~30 calls/frame upload. The rest
only confirm that every page is already valid, yet each one builds, sorts and frees a
std::vector. Fast path for count == 1: the same bounds checks, the same
EnsureHostGpuMemoryAllocated, then (under the same lock) a test of the requested pages'
valid bits. All valid -> exactly what the original does when its upload list comes out
empty: OnRangesRequested({start, length}) and return true. Any page invalid -> the
original path, unchanged.

Proof (TEST ONLY) ht_request_range_fast_verify: after each fast "all valid" decision the
original path also runs for the same range; every time it wants to upload is counted:
  [ht-rrfast-verify] fast_hits=N original_uploads=0   (must stay 0)
Positive control ht_request_range_fast_verify_break: the fast check ignores the LAST
page of each range (wrong on purpose) -> original_uploads must become > 0.

usage: python3 -I patch_request_range_fast.py /root/ht/sdk   (on top of the 8.9 SDK)
"""
import sys
from pathlib import Path

sdk = Path(sys.argv[1])
p = sdk / 'src/graphics/shared_memory.cpp'
s = p.read_text()

def rep(old, new):
    global s
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)

rep('#include <rex/assert.h>\n',
    '#include <rex/assert.h>\n#include <rex/cvar.h>\n#include <rex/logging.h>\n')
rep('#include <rex/memory.h>\n', '''#include <rex/memory.h>

REXCVAR_DEFINE_BOOL(ht_request_range_fast, true, "GPU",
                    "Single-range shared memory requests: skip the merge/sort when every page is already valid");
REXCVAR_DEFINE_BOOL(ht_request_range_fast_verify, false, "GPU",
                    "TEST ONLY: after each fast hit also run the original request path and count uploads it wanted");
REXCVAR_DEFINE_BOOL(ht_request_range_fast_verify_break, false, "GPU",
                    "TEST ONLY: positive control - the fast check ignores the last page (must make verify fail)");
''')

rep('bool SharedMemory::RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count) {\n',
    '''namespace {
// Command processor thread only (like the rest of RequestRanges' callers).
thread_local bool ht_rr_in_verify = false;
uint64_t ht_rr_fast_hits = 0, ht_rr_verify_uploads = 0;
}  // namespace

bool SharedMemory::RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count) {
  if (count == 1 && !ht_rr_in_verify && REXCVAR_GET(ht_request_range_fast)) {
    const uint32_t start = ranges[0].first, length = ranges[0].second;
    if (!length) {
      return true;
    }
    if (start > kBufferSize || (kBufferSize - start) < length) {
      return false;
    }
    if (!EnsureHostGpuMemoryAllocated(start, length)) {
      return false;
    }
    const uint32_t page_first = start >> page_size_log2_;
    const uint32_t page_last = (start + length - 1) >> page_size_log2_;
    const bool verify_break = REXCVAR_GET(ht_request_range_fast_verify_break);
    // Proof mode holds the (recursive) global lock from the fast check through the
    // re-run of the original path, so a guest CPU write landing in between cannot be
    // mistaken for a wrong fast decision (seen once at boot on Linux before this).
    std::unique_lock<std::recursive_mutex> verify_lock;
    if (REXCVAR_GET(ht_request_range_fast_verify)) {
      verify_lock = global_critical_region_.Acquire();
    }
    bool all_valid = true;
    {
      auto global_lock = global_critical_region_.Acquire();
      for (uint32_t i = page_first >> 6; i <= (page_last >> 6); ++i) {
        uint64_t mask = UINT64_MAX;
        if (i == (page_first >> 6)) {
          mask &= ~((uint64_t(1) << (page_first & 63)) - 1);
        }
        if (i == (page_last >> 6) && (page_last & 63) != 63) {
          mask &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;
        }
        if (verify_break && i == (page_last >> 6)) {
          mask &= ~(uint64_t(1) << (page_last & 63));
        }
        if ((system_page_flags_valid_[i] & mask) != mask) {
          all_valid = false;
          break;
        }
      }
    }
    if (all_valid) {
      if (REXCVAR_GET(ht_request_range_fast_verify)) {
        ++ht_rr_fast_hits;
        const uint64_t uploads_before = ht_rr_verify_uploads;
        ht_rr_in_verify = true;
        const bool ok = RequestRanges(ranges, count);
        ht_rr_in_verify = false;
        if ((ht_rr_fast_hits & 0x3FFFF) == 0 || ht_rr_verify_uploads != uploads_before) {
          REXGPU_WARN("[ht-rrfast-verify] fast_hits={} original_uploads={}", ht_rr_fast_hits,
                      ht_rr_verify_uploads);
        }
        return ok;
      }
      static thread_local std::vector<std::pair<uint32_t, uint32_t>> single(1);
      single[0] = std::make_pair(start, length);
      OnRangesRequested(single);
      return true;
    }
  }
''')

rep('  bool uploaded = UploadRanges(upload_ranges_);\n',
    '  if (ht_rr_in_verify) {\n    ++ht_rr_verify_uploads;\n  }\n  bool uploaded = UploadRanges(upload_ranges_);\n')
p.write_text(s)
print('patched ht_request_range_fast')
