#pragma once
//
// CPU: give the guest's spin-wait back-off a real pause (ht_spin_backoff).
//
// sub_822C7C60 is the game's (D3D runtime's) spin-wait step. On the Xbox 360 its
// body is `cctpl` (drop the hardware thread priority), 32 x `db16cyc` (16-cycle
// delays) and `cctpm`, then one check of the wait condition / 5000-tick timeout.
// Recompiled, the priority hints and delays are no-ops, so every wait loop that
// calls it (sub_822D8570: wait for the GPU to consume the ring buffer, and the
// other ring/fence waits) spins at full speed. Measured at 60 FPS: one guest
// thread burns a whole host core (~1000 ms CPU per second), almost all of it in
// this loop while the emulated GPU is pacing to 60 FPS.
//
// The hook keeps the original function and its result; it only adds the pause the
// hardware had, in steps: the first calls of a wait use the CPU pause instruction,
// then the thread yields, then it sleeps briefly between checks. A wait that ends
// is detected by the calls stopping; the next wait starts again from the pause.
// Guest logic is unchanged (the condition and the timeout are still checked by the
// original code on every call); only how long each spin step takes changes.
// Cvars: ht_spin_backoff (default true), ht_spin_backoff_sleep_us (sleep step).

#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/thread.h>

#include "hydrothunder_init.h"

REXCVAR_DEFINE_BOOL(ht_spin_backoff, true, "Hydro Thunder",
                    "Pause between the game's spin-wait steps instead of spinning a full core");
REXCVAR_DEFINE_INT32(ht_spin_backoff_sleep_us, 200, "Hydro Thunder",
                     "Sleep step (microseconds) of a long spin wait");

namespace ht {

inline void SpinBackoffStep() {
  // Calls closer together than this belong to the same wait.
  constexpr int64_t kSameWaitNs = 2'000'000;
  constexpr uint32_t kPauseSteps = 64;
  constexpr uint32_t kYieldSteps = 128;
  thread_local uint32_t steps = 0;
  thread_local int64_t last_ns = 0;
  const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (now - last_ns > kSameWaitNs) {
    steps = 0;
  }
  ++steps;
  if (steps <= kPauseSteps) {
#if defined(_M_X64) || defined(__x86_64__)
    for (int i = 0; i < 8; ++i) _mm_pause();
#endif
  } else if (steps <= kYieldSteps) {
    std::this_thread::yield();
  } else {
    rex::thread::NanoSleep(int64_t(REXCVAR_GET(ht_spin_backoff_sleep_us)) * 1000);
  }
  last_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
}

}  // namespace ht

REX_HOOK_RAW(sub_822C7C60) {
  if (REXCVAR_GET(ht_spin_backoff)) {
    ht::SpinBackoffStep();
  }
  rex_original_sub_822C7C60(ctx, base);
}
