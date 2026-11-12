#!/usr/bin/env python3
"""MEASUREMENT ONLY: why the Audio Worker thread spins (ROG Ally run: 77% of a core).
Counts, per 10 s, for WaitMultiple: calls, loop turns, successes, timeouts, contended
(trylock failed -> yield), sleeps; and for AudioSystem::WorkerThreadMain: pumps by result.
usage: patch_measure_waitmultiple.py <sdk root>
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
f = root / "src/core/threading_posix.cpp"
s = f.read_text()
if "ht_wm_" not in s:
    head = ("#include <atomic>\n#include <rex/logging.h>\n"
            "namespace ht_wm { inline std::atomic<uint64_t> calls{0}, turns{0}, ok{0}, tmo{0}, cont{0}, sleeps{0}, single{0};\n"
            "inline void report() {\n"
            "  static std::atomic<int64_t> last{0};\n"
            "  const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();\n"
            "  int64_t l = last.load();\n"
            "  if (now - l < 10 || !last.compare_exchange_strong(l, now)) return;\n"
            "  REXLOG_WARN(\"[ht-wm] 10s: calls={} single={} turns={} ok={} timeout={} contended={} sleeps={}\", calls.exchange(0), single.exchange(0), turns.exchange(0), ok.exchange(0), tmo.exchange(0), cont.exchange(0), sleeps.exchange(0));\n"
            "} }  // ht_wm_\n")
    i = s.find("namespace rex")
    s = s[:i] + head + s[i:]
    pairs = [
        ("    assert_true(!handles.empty());\n\n    if (handles.size() == 1) {\n",
         "    assert_true(!handles.empty());\n    ++ht_wm::calls; ht_wm::report();\n\n    if (handles.size() == 1) {\n      ++ht_wm::single;\n"),
        ("    while (true) {\n      size_t first_signaled = std::numeric_limits<size_t>::max();\n",
         "    while (true) {\n      ++ht_wm::turns;\n"
         "      { thread_local uint64_t ht_my = 0; if (++ht_my % 5000000 == 0) {\n"
         "          char ht_n[32] = {}; pthread_getname_np(pthread_self(), ht_n, sizeof(ht_n));\n"
         "          REXLOG_WARN(\"[ht-wm] spinner thread '{}' turns={} handles={} timeout_ms={}\", ht_n, ht_my, handles.size(), int64_t(timeout.count())); } }\n"
         "      size_t first_signaled = std::numeric_limits<size_t>::max();\n"),
        ("      if (!all_locked) {\n        locks.clear();\n        std::this_thread::yield();\n",
         "      if (!all_locked) {\n        ++ht_wm::cont;\n        locks.clear();\n        std::this_thread::yield();\n"),
        ("        return std::make_pair(WaitResult::kSuccess, first_signaled);\n      }\n",
         "        ++ht_wm::ok;\n        return std::make_pair(WaitResult::kSuccess, first_signaled);\n      }\n"),
        ("      if (now >= end_time) {\n        return std::make_pair<WaitResult, size_t>(WaitResult::kTimeout, 0);\n",
         "      if (now >= end_time) {\n        ++ht_wm::tmo;\n        return std::make_pair<WaitResult, size_t>(WaitResult::kTimeout, 0);\n"),
        ("      if (timeout == std::chrono::milliseconds::max()) {\n        std::this_thread::sleep_for(std::chrono::milliseconds(1));\n",
         "      ++ht_wm::sleeps;\n      if (timeout == std::chrono::milliseconds::max()) {\n        std::this_thread::sleep_for(std::chrono::milliseconds(1));\n"),
    ]
    for a, b in pairs:
        assert s.count(a) == 1, a[:60]
        s = s.replace(a, b)
    f.write_text(s)
print("threading_posix.cpp: WaitMultiple counters (measurement only)")
