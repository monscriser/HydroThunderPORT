"""MEASUREMENT ONLY (revert after use): CPU time of recording + submitting each Vulkan
command buffer in EndSubmission (vkResetCommandPool .. vkQueueSubmit), split into mid-frame
and swap submissions, plus deferred command buffer size. Logs [ht-submit] every ~2 s.

usage: python3 patch_measure_submit.py /root/ht/sdk
"""
import sys
from pathlib import Path

p = Path(sys.argv[1]) / "src/graphics/vulkan/command_processor.cpp"
s = p.read_text()
old = """    assert_false(command_buffers_writable_.empty());
    CommandBuffer command_buffer = command_buffers_writable_.back();
    if (dfn.vkResetCommandPool(device, command_buffer.pool, 0) != VK_SUCCESS) {"""
new = """    const auto ht_ms_t0 = std::chrono::steady_clock::now();
    static double ht_ms_mid = 0, ht_ms_swap = 0, ht_ms_exec = 0;
    static uint64_t ht_ms_n_mid = 0, ht_ms_n_swap = 0, ht_ms_frames = 0, ht_ms_bytes = 0;
    static auto ht_ms_last = std::chrono::steady_clock::now();
    ht_ms_bytes += deferred_command_buffer_.HtSizeBytes();
    assert_false(command_buffers_writable_.empty());
    CommandBuffer command_buffer = command_buffers_writable_.back();
    if (dfn.vkResetCommandPool(device, command_buffer.pool, 0) != VK_SUCCESS) {"""
assert s.count(old) == 1
s = s.replace(old, new)
old = """    deferred_command_buffer_.Execute(command_buffer.buffer);
    if (dfn.vkEndCommandBuffer(command_buffer.buffer) != VK_SUCCESS) {"""
new = """    const auto ht_ms_e0 = std::chrono::steady_clock::now();
    deferred_command_buffer_.Execute(command_buffer.buffer);
    ht_ms_exec += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ht_ms_e0).count();
    if (dfn.vkEndCommandBuffer(command_buffer.buffer) != VK_SUCCESS) {"""
assert s.count(old) == 1
s = s.replace(old, new)
old = """    uint64_t submission_current = GetCurrentSubmission();
    current_submission_wait_stage_masks_.clear();"""
new = """    {
      const double ht_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ht_ms_t0).count();
      if (is_swap) { ht_ms_swap += ht_ms; ++ht_ms_n_swap; if (frame_open_) ++ht_ms_frames; }
      else { ht_ms_mid += ht_ms; ++ht_ms_n_mid; }
      if (std::chrono::steady_clock::now() - ht_ms_last > std::chrono::seconds(2) && ht_ms_frames) {
        const double f = double(ht_ms_frames);
        REXGPU_WARN("[ht-submit] per frame: record+submit mid={:.3f} ms ({:.2f} subm) swap={:.3f} ms ({:.2f} subm) execute={:.3f} ms deferred_kb={:.0f}",
                    ht_ms_mid / f, ht_ms_n_mid / f, ht_ms_swap / f, ht_ms_n_swap / f, ht_ms_exec / f, ht_ms_bytes / f / 1024.0);
        ht_ms_mid = ht_ms_swap = ht_ms_exec = 0; ht_ms_n_mid = ht_ms_n_swap = ht_ms_frames = ht_ms_bytes = 0;
        ht_ms_last = std::chrono::steady_clock::now();
      }
    }
    uint64_t submission_current = GetCurrentSubmission();
    current_submission_wait_stage_masks_.clear();"""
assert s.count(old) == 1
s = s.replace(old, new)
p.write_text(s)
h = Path(sys.argv[1]) / "include/rex/graphics/vulkan/deferred_command_buffer.h"
t = h.read_text()
marker = "  void Execute(VkCommandBuffer command_buffer);"
assert t.count(marker) == 1, "Execute decl"
t = t.replace(marker, marker + "\n  size_t HtSizeBytes() const { return command_stream_.size() * sizeof(uintmax_t); }")
h.write_text(t)
print("patched submit measurement")
