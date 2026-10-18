"""Measurement only: GPU time of every draw of the traced frame (HT_TRACE_FRAME=<n>,
needs ht_perf_log=true). A bottom-of-pipe timestamp after each draw; the delta to
the previous draw of the same submission is logged as [ht-tdraw] once the frame
completed: pre= is the work issued between the two draws (barriers, resolves,
transfers, clears), us= the draw itself (from a timestamp written right before it).

usage: python3 patch_draw_timing.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new, count=1):
    s = path.read_text()
    assert s.count(old) == count, (path, s.count(old), old[:120])
    path.write_text(s.replace(old, new))


h = root / "include/rex/graphics/vulkan/command_processor.h"
patch(h, "  VkQueryPool perf_query_pool_ = VK_NULL_HANDLE;\n",
      "  VkQueryPool perf_query_pool_ = VK_NULL_HANDLE;\n"
      "  // ht draw timing (HT_TRACE_FRAME).\n"
      "  static constexpr uint32_t kHtTq = 16384;\n"
      "  VkQueryPool ht_tq_pool_ = VK_NULL_HANDLE;\n"
      "  bool ht_tq_reset_ = false;\n"
      "  uint32_t ht_tq_count_ = 0;\n"
      "  std::vector<std::pair<uint64_t, uint64_t>> ht_tq_meta_;\n"
      "  uint64_t ht_tq_done_submission_ = 0;\n")

cp = root / "src/graphics/vulkan/command_processor.cpp"
patch(cp, """  perf_window_start_ = std::chrono::steady_clock::now();
  perf_last_swap_ = perf_window_start_;
}""", """  perf_window_start_ = std::chrono::steady_clock::now();
  perf_last_swap_ = perf_window_start_;
  if (std::getenv("HT_TRACE_FRAME")) {
    pool_info.queryCount = kHtTq;
    if (vulkan_device->functions().vkCreateQueryPool(vulkan_device->device(), &pool_info, nullptr,
                                                     &ht_tq_pool_) != VK_SUCCESS) {
      ht_tq_pool_ = VK_NULL_HANDLE;
    }
  }
}""")
patch(cp, """  deferred_command_buffer_.CmdVkResetQueryPool(perf_query_pool_, perf_block_base_, kPerfBlockDeferred);
  perf_open_.fill(UINT32_MAX);""", """  deferred_command_buffer_.CmdVkResetQueryPool(perf_query_pool_, perf_block_base_, kPerfBlockDeferred);
  if (ht_tq_pool_ != VK_NULL_HANDLE && ht_trace_active && !ht_tq_reset_) {
    deferred_command_buffer_.CmdVkResetQueryPool(ht_tq_pool_, 0, kHtTq);
    ht_tq_reset_ = true;
    ht_tq_count_ = 0;
    ht_tq_meta_.clear();
  }
  perf_open_.fill(UINT32_MAX);""")
patch(cp, """  // Draw.
  if (primitive_processing_result.index_buffer_type ==""", """  // Draw.
  if (ht_tq_reset_ && ht_trace_active && ht_tq_count_ + 1 < kHtTq) {
    deferred_command_buffer_.CmdVkWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ht_tq_pool_,
                                                 ht_tq_count_++);
    ht_tq_meta_.emplace_back(UINT64_MAX, GetCurrentSubmission());
  }
  if (primitive_processing_result.index_buffer_type ==""")
patch(cp, """    deferred_command_buffer_.CmdVkDrawIndexed(primitive_processing_result.host_draw_vertex_count, 1,
                                              0, 0, 0);
  }
""", """    deferred_command_buffer_.CmdVkDrawIndexed(primitive_processing_result.host_draw_vertex_count, 1,
                                              0, 0, 0);
  }
  if (ht_tq_reset_ && ht_trace_active && ht_tq_count_ < kHtTq) {
    deferred_command_buffer_.CmdVkWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ht_tq_pool_,
                                                 ht_tq_count_++);
    ht_tq_meta_.emplace_back(uint64_t(ht_trace_draw), GetCurrentSubmission());
  }
""")
patch(cp, """    } else if (ht_trace_active) {
      ht_trace_active = 0;
      REXGPU_WARN("[ht-trace] ===== frame end, draws={} =====", uint64_t(ht_trace_draw));
    }
  }""", """    } else if (ht_trace_active) {
      ht_trace_active = 0;
      REXGPU_WARN("[ht-trace] ===== frame end, draws={} =====", uint64_t(ht_trace_draw));
      ht_tq_done_submission_ = GetCurrentSubmission();
    }
    if (ht_tq_done_submission_ && ht_tq_count_ &&
        GetCompletedSubmission() >= ht_tq_done_submission_) {
      const ui::vulkan::VulkanDevice* ht_dev = GetVulkanDevice();
      const double ht_us = double(ht_dev->properties().timestampPeriod) * 1e-3;
      std::vector<uint64_t> ht_t(ht_tq_count_);
      if (ht_dev->functions().vkGetQueryPoolResults(
              ht_dev->device(), ht_tq_pool_, 0, ht_tq_count_, sizeof(uint64_t) * ht_tq_count_,
              ht_t.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        double ht_total = 0.0;
        auto ht_delta = [&](uint32_t a, uint32_t b) {
          return ht_tq_meta_[a].second == ht_tq_meta_[b].second && ht_t[b] >= ht_t[a]
                     ? double(ht_t[b] - ht_t[a]) * ht_us
                     : -1.0;
        };
        for (uint32_t i = 1; i < ht_tq_count_; ++i) {
          if (ht_tq_meta_[i].first == UINT64_MAX) continue;
          // i - 1 is the timestamp right before this draw, i - 2 the end of the previous draw.
          const double us = ht_delta(i - 1, i);
          const double pre = i >= 2 ? ht_delta(i - 2, i - 1) : -1.0;
          if (us > 0.0) ht_total += us;
          if (pre > 0.0) ht_total += pre;
          REXGPU_WARN("[ht-tdraw] draw={} subm={} us={:.1f} pre={:.1f}", ht_tq_meta_[i].first,
                      ht_tq_meta_[i].second, us, pre);
        }
        REXGPU_WARN("[ht-tdraw] total_us={:.1f} timestamps={}", ht_total, ht_tq_count_);
      }
      ht_tq_done_submission_ = 0;
      ht_tq_count_ = 0;
      ht_tq_reset_ = false;
    }
  }""")
print("patched draw timing")
