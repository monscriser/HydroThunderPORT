#pragma once
#include <chrono>
#include <rex/thread.h>
namespace rex::graphics {
// Pace guest swaps independently of the console's vblank clock. Never speed
// up simulation or replay missed deadlines after a shader/loading stall.
class FrameLimiter {
 public:
  void Pace(int limit, double vblank_hz) {
    // Hydro Thunder swaps as fast as it can (it does not wait for vblank), so
    // the limiter is what holds the swap rate at the selected FPS.
    (void)vblank_hz;
    if (limit <= 0) {
      rate_ = 0;
      return;
    }
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const auto interval = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / limit));
    if (rate_ != limit) {
      rate_ = limit;
      next_ = now + interval;
      return;
    }
    if (now > next_ + interval) {
      next_ = now + interval;
      return;
    }
    if (now < next_) {
      auto remaining = next_ - now;
      const auto margin = std::chrono::microseconds(500);
      if (remaining > margin) {
        rex::thread::NanoSleep(
            std::chrono::duration_cast<std::chrono::nanoseconds>(remaining - margin).count());
      }
      while (Clock::now() < next_) rex::thread::MaybeYield();
    }
    next_ += interval;
  }

 private:
  int rate_ = 0;
  std::chrono::steady_clock::time_point next_{};
};
}  // namespace rex::graphics
