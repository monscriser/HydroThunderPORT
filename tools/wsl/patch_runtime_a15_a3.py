#!/usr/bin/env python3
"""Hydro 9.1.x: two runtime fixes ported from UFC3MONS BETA 1.2, each with a counter.

A15 ht_resume_running_noop (Windows): resuming a guest thread whose suspend count is already 0
  no longer wraps the count to 255 (the next suspend would be lost) nor asks the host; same as the
  console and as the Linux path of the SDK. [ht-a15] resume_of_running_thread= (powers of two).
A3 ht_xma_loop_end_hw_layout: the XMA loop end subframe is read from DWORD1 bits 14-16 (the bits
  XMASetLoopData/hardware write, Xenia Canary layout) instead of bits 12-13.
  [ht-a3] loop_end_frames= differ= counts loop-end frames where the two fields disagree.
Usage: patch_runtime_a15_a3.py <sdk root>"""
import sys, os

sdk = sys.argv[1]
XT = os.path.join(sdk, "src/system/xthread.cpp")
XC = os.path.join(sdk, "src/audio/xma_context.cpp")


def edit(path, pairs):
    s = open(path, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    s = s.replace("\r\n", "\n")
    for old, new in pairs:
        n = s.count(old)
        if n != 1:
            sys.exit(f"{os.path.basename(path)}: anchor found {n} times: {old[:80]!r}")
        s = s.replace(old, new)
    if crlf:
        s = s.replace("\n", "\r\n")
    open(path, "w", encoding="utf-8", newline="").write(s)
    print("patched", path)


edit(XT, [
    ('REXCVAR_DEFINE_BOOL(ignore_thread_affinities, true, "Kernel",\n'
     '                    "Ignores game-specified thread affinities");\n',
     'REXCVAR_DEFINE_BOOL(ignore_thread_affinities, true, "Kernel",\n'
     '                    "Ignores game-specified thread affinities");\n'
     'REXCVAR_DEFINE_BOOL(ht_resume_running_noop, true, "Kernel",\n'
     '                    "A15: Resume of a thread that is not suspended is a no-op on Windows");\n'),
    ('''  uint8_t previous_suspend_count =
      reinterpret_cast<std::atomic_uint8_t*>(&guest_thread->suspend_count)->fetch_sub(1);
  if (out_suspend_count) {
    *out_suspend_count = previous_suspend_count;
  }
  return thread_->Resume(&unused_host_suspend_count) ? X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
''', '''  auto& suspend_count = *reinterpret_cast<std::atomic_uint8_t*>(&guest_thread->suspend_count);
  uint8_t previous_suspend_count;
  const bool noop = REXCVAR_GET(ht_resume_running_noop);
  if (noop) {
    // A15: resuming a thread that is not suspended changes nothing, as on the console (and as
    // the Linux path below): the count must not wrap to 255 and the host is not asked.
    previous_suspend_count = suspend_count.load(std::memory_order_relaxed);
    while (previous_suspend_count &&
           !suspend_count.compare_exchange_weak(previous_suspend_count,
                                                uint8_t(previous_suspend_count - 1))) {
    }
  } else {
    previous_suspend_count = suspend_count.fetch_sub(1);
  }
  if (!previous_suspend_count) {
    static std::atomic<uint64_t> ht_running_resumes{0};
    const uint64_t n = ++ht_running_resumes;
    if ((n & (n - 1)) == 0) {
      REXSYS_WARN("[ht-a15] resume_of_running_thread={} noop={}", n, noop);
    }
  }
  if (out_suspend_count) {
    *out_suspend_count = previous_suspend_count;
  }
  if (!previous_suspend_count && noop) {
    return X_STATUS_SUCCESS;
  }
  return thread_->Resume(&unused_host_suspend_count) ? X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
'''),
])

edit(XC, [
    ('#include <rex/dbg.h>\n', '#include <atomic>\n#include <rex/cvar.h>\n#include <rex/dbg.h>\n'),
    ('// Credits for most of this code goes to:\n',
     'REXCVAR_DEFINE_BOOL(ht_xma_loop_end_hw_layout, true, "Audio",\n'
     '                    "A3: read the XMA loop end subframe from DWORD1 bits 14-16 (hardware layout)");\n'
     '\n// Credits for most of this code goes to:\n'),
    ('''    const uint8_t decoded_output_limit =
        is_loop_end_frame ? static_cast<uint8_t>((data->loop_subframe_end + 1) << data->is_stereo)
                          : 0;
''', '''    if (is_loop_end_frame) {
      static std::atomic<uint64_t> ht_loop_ends{0}, ht_loop_differ{0};
      const uint64_t n = ++ht_loop_ends;
      if (uint32_t(data->unk_dword_1_a) != uint32_t(data->loop_subframe_end)) ++ht_loop_differ;
      if ((n & (n - 1)) == 0) {
        REXLOG_WARN("[ht-a3] loop_end_frames={} differ={} hw_layout={}", n, ht_loop_differ.load(),
                    REXCVAR_GET(ht_xma_loop_end_hw_layout));
      }
    }
    const uint32_t loop_end_subframe = REXCVAR_GET(ht_xma_loop_end_hw_layout)
                                           ? uint32_t(data->unk_dword_1_a)
                                           : uint32_t(data->loop_subframe_end);
    const uint8_t decoded_output_limit =
        is_loop_end_frame ? static_cast<uint8_t>((loop_end_subframe + 1) << data->is_stereo) : 0;
'''),
])
