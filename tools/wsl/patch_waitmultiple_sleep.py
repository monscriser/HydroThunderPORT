#!/usr/bin/env python3
"""CPU (Linux): WaitMultiple busy-spin fix (ht_wait_precise_sleep, default true).

ROG Ally run (11/11/2026, 20 W): the Audio Worker thread used 77% of a core on every
track. Measured in WSL with patch_measure_waitmultiple.py: ~1,230 WaitMultiple calls/s
but ~4.6 MILLION loop turns/s and ~670 timeouts/s. Cause: the polling loop sleeps
    min(duration_cast<milliseconds>(end_time - now), 1 ms)
and the cast FLOORS: during the last millisecond before a timeout it sleeps 0 ms, so
the thread spins (trylock all handles, check, unlock, clock_gettime) until the deadline.
Fix: sleep the exact remaining time (steady_clock resolution), still capped at 1 ms, so
the wake-up times and the polling granularity are what the original code intended.
usage: patch_waitmultiple_sleep.py <sdk root>
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
f = root / "src/core/threading_posix.cpp"
s = f.read_text()
MARK = "ht_wait_precise_sleep"
if MARK in s:
    print("threading_posix.cpp: already patched")
    sys.exit(0)
old = ("        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - now);\n"
       "        auto sleep_time = std::min(remaining, std::chrono::milliseconds(1));\n"
       "        std::this_thread::sleep_for(sleep_time);\n")
new = ("        if (REXCVAR_GET(ht_wait_precise_sleep)) {\n"
       "          // Exact remaining time: the millisecond cast floored to 0 in the last ms\n"
       "          // before the deadline and the loop spun (Audio Worker at 77% of a core).\n"
       "          const auto remaining = end_time - now;\n"
       "          std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(\n"
       "              remaining, std::chrono::milliseconds(1)));\n"
       "        } else {\n"
       "        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - now);\n"
       "        auto sleep_time = std::min(remaining, std::chrono::milliseconds(1));\n"
       "        std::this_thread::sleep_for(sleep_time);\n"
       "        }\n")
assert s.count(old) == 1, "sleep anchor"
s = s.replace(old, new)
inc = "namespace rex"
i = s.find(inc)
s = (s[:i] + "#include <rex/cvar.h>\n"
     "REXCVAR_DEFINE_BOOL(ht_wait_precise_sleep, true, \"CPU\",\n"
     "                    \"WaitMultiple sleeps the exact remaining time (no 0 ms busy spin)\");\n\n" + s[i:])
f.write_text(s)
print("threading_posix.cpp: WaitMultiple precise sleep")
