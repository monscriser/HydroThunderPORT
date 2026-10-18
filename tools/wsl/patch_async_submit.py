"""9.0.x CPU: record + submit mid-frame Vulkan command buffers on a worker thread.

ht_async_submit (default true): EndSubmission(false) hands the finished deferred command
stream (and the hoisted upload stream) to a worker thread, which records the real
VkCommandBuffer (DeferredCommandBuffer::ExecuteStream) and calls vkQueueSubmit, in order.
The command processor thread keeps building the next submission meanwhile.
 - The fence is reset on the command processor thread before the hand-off; bookkeeping
   (in-flight fences, command pools, semaphores) is identical to the synchronous path.
 - Swap submissions (guest output, read by the presenter right after its own empty submit)
   and any submission while the worker failed stay synchronous after draining the worker.
 - CheckSubmissionFenceAndDeviceLoss drains the worker before any blocking wait and only
   polls fences of jobs already submitted; sparse binds and shutdown also drain it.
 - A worker submission failure is treated as device loss (its fence would never signal).
ht_async_submit_verify (TEST ONLY): hash of the streams at hand-off vs. after the worker
recorded them ([ht-async-submit] ... verified= mismatches=): proves the streams were not
modified while being recorded. ht_async_submit_verify_break: perturbs the hand-off hash.

usage: python3 patch_async_submit.py /root/ht/sdk   (on the 9.0.0 cumulative SDK)
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def sub(path, old, new, count=1):
    p = root / path
    s = p.read_text()
    assert s.count(old) == count, (path, old[:90], s.count(old))
    p.write_text(s.replace(old, new))


# --- DeferredCommandBuffer: execute an external stream, swap the stream out ------------
sub("include/rex/graphics/vulkan/deferred_command_buffer.h",
    "  void Execute(VkCommandBuffer command_buffer);\n",
    "  void Execute(VkCommandBuffer command_buffer);\n"
    "  // ht_async_submit: record a stream taken out of a DeferredCommandBuffer (thread-safe:\n"
    "  // reads only the stream and the device functions).\n"
    "  void ExecuteStream(VkCommandBuffer command_buffer, const uintmax_t* stream_data,\n"
    "                     size_t stream_size) const;\n"
    "  void HtSwapStream(std::vector<uintmax_t>& other) { command_stream_.swap(other); }\n"
    "  const uintmax_t* HtData() const { return command_stream_.data(); }\n"
    "  size_t HtSize() const { return command_stream_.size(); }\n")
sub("src/graphics/vulkan/deferred_command_buffer.cpp",
    "void DeferredCommandBuffer::Execute(VkCommandBuffer command_buffer) {\n",
    "void DeferredCommandBuffer::Execute(VkCommandBuffer command_buffer) {\n"
    "  ExecuteStream(command_buffer, command_stream_.data(), command_stream_.size());\n"
    "}\n\n"
    "void DeferredCommandBuffer::ExecuteStream(VkCommandBuffer command_buffer,\n"
    "                                          const uintmax_t* stream_data,\n"
    "                                          size_t stream_size) const {\n")
sub("src/graphics/vulkan/deferred_command_buffer.cpp",
    "  const uintmax_t* stream = command_stream_.data();\n  size_t stream_remaining = command_stream_.size();\n",
    "  const uintmax_t* stream = stream_data;\n  size_t stream_remaining = stream_size;\n")

# --- header: worker state ---------------------------------------------------------------
H = "include/rex/graphics/vulkan/command_processor.h"
sub(H, "#include <array>\n#include <chrono>\n#include <climits>\n",
    "#include <array>\n#include <atomic>\n#include <chrono>\n#include <climits>\n#include <condition_variable>\n#include <mutex>\n#include <thread>\n")
sub(H, "  std::deque<std::pair<uint64_t, CommandBuffer>> command_buffers_submitted_;\n",
    """  std::deque<std::pair<uint64_t, CommandBuffer>> command_buffers_submitted_;

  // ht_async_submit: mid-frame submissions recorded + submitted on a worker thread.
  struct HtSubmitJob {
    CommandBuffer command_buffer;
    VkFence fence;
    std::vector<VkSemaphore> wait_semaphores;
    std::vector<VkPipelineStageFlags> wait_stages;
    std::vector<uintmax_t> upload_stream, main_stream;
    bool upload_used = false, perf_upload = false;
    uint32_t perf_upload_begin = 0;
    uint64_t hash = 0;
  };
  bool HtRecordAndSubmit(const CommandBuffer& command_buffer, VkFence fence, bool reset_fence,
                         const std::vector<VkSemaphore>& wait_semaphores,
                         const std::vector<VkPipelineStageFlags>& wait_stages, bool upload_used,
                         const uintmax_t* upload_data, size_t upload_size,
                         const uintmax_t* main_data, size_t main_size, bool perf_upload,
                         uint32_t perf_upload_begin, VkResult* result_out);
  void HtSubmitWorkerMain();
  void HtFlushSubmitWorker();
  void HtStopSubmitWorker();
  size_t HtUnsubmittedJobs();
  static uint64_t HtStreamHash(const std::vector<uintmax_t>& a, const std::vector<uintmax_t>& b);
  bool ht_async_submit_enabled_ = false;
  std::thread ht_submit_thread_;
  std::mutex ht_submit_mutex_;
  std::condition_variable ht_submit_cv_, ht_submit_done_cv_;
  std::deque<HtSubmitJob> ht_submit_queue_;
  size_t ht_submit_pending_ = 0;
  bool ht_submit_stop_ = false;
  std::atomic<bool> ht_submit_failed_{false};
  std::atomic<uint64_t> ht_async_verified_{0}, ht_async_mismatches_{0};
  std::vector<std::vector<uintmax_t>> ht_stream_free_;
  uint64_t ht_async_jobs_ = 0, ht_sync_submits_ = 0, ht_async_flushes_ = 0;
  std::chrono::steady_clock::time_point ht_async_log_time_{};
""")

# --- cpp ----------------------------------------------------------------------------------
C = "src/graphics/vulkan/command_processor.cpp"
sub(C, 'REXCVAR_DEFINE_BOOL(ht_dynamic_constants, true, "GPU/Vulkan",\n',
    'REXCVAR_DEFINE_BOOL(ht_async_submit, true, "GPU/Vulkan",\n'
    '                    "CPU: record and submit mid-frame command buffers on a worker thread");\n'
    'REXCVAR_DEFINE_BOOL(ht_async_submit_verify, false, "GPU/Vulkan",\n'
    '                    "TEST ONLY: hash streams at hand-off and after recording on the worker");\n'
    'REXCVAR_DEFINE_BOOL(ht_async_submit_verify_break, false, "GPU/Vulkan",\n'
    '                    "TEST ONLY: perturb the hand-off hash of the async submit verifier");\n'
    'REXCVAR_DEFINE_BOOL(ht_dynamic_constants, true, "GPU/Vulkan",\n')
sub(C, "VulkanCommandProcessor::~VulkanCommandProcessor() = default;\n",
    "VulkanCommandProcessor::~VulkanCommandProcessor() { HtStopSubmitWorker(); }\n")
sub(C, "  ht_dynamic_constants_enabled_ = REXCVAR_GET(ht_dynamic_constants);\n",
    """  ht_dynamic_constants_enabled_ = REXCVAR_GET(ht_dynamic_constants);
  ht_async_submit_enabled_ = REXCVAR_GET(ht_async_submit);
  if (ht_async_submit_enabled_ && !ht_submit_thread_.joinable()) {
    ht_submit_stop_ = false;
    ht_submit_failed_ = false;
    ht_submit_thread_ = std::thread([this]() { HtSubmitWorkerMain(); });
  }
  REXGPU_WARN("[ht-async-submit] enabled={}", ht_async_submit_enabled_);
""")
sub(C, "void VulkanCommandProcessor::ShutdownContext() {\n  AwaitAllQueueOperationsCompletion();\n",
    "void VulkanCommandProcessor::ShutdownContext() {\n  AwaitAllQueueOperationsCompletion();\n  HtStopSubmitWorker();\n")

# CheckSubmissionFenceAndDeviceLoss: drain before blocking waits, poll only submitted fences.
sub(C, """  size_t fences_total = submissions_in_flight_fences_.size();
  size_t fences_awaited = 0;""",
    """  size_t ht_unsubmitted = 0;
  if (ht_async_submit_enabled_) {
    if (await_submission > submission_completed_) {
      HtFlushSubmitWorker();
    }
    ht_unsubmitted = HtUnsubmittedJobs();
    if (ht_submit_failed_.load()) {
      // A worker submission failed: its fence never signals - same as device loss.
      device_lost_ = true;
      if (graphics_system_) {
        graphics_system_->OnHostGpuLossFromAnyThread(true);
      }
      return;
    }
  }
  size_t fences_total = submissions_in_flight_fences_.size() - ht_unsubmitted;
  size_t fences_awaited = 0;""")

# Sparse binds go to the queue directly: keep queue order.
sub(C, """    // Submit sparse binds earlier, before executing the deferred command
    // buffer, to reduce latency.
    if (!sparse_memory_binds_.empty()) {""",
    """    // Submit sparse binds earlier, before executing the deferred command
    // buffer, to reduce latency.
    if (ht_async_submit_enabled_ && !sparse_memory_binds_.empty()) {
      HtFlushSubmitWorker();
    }
    if (!sparse_memory_binds_.empty()) {""")

# EndSubmission: replace recording + submit with the shared function / worker hand-off.
p = root / C
s = p.read_text()
start_marker = """    assert_false(command_buffers_writable_.empty());
    CommandBuffer command_buffer = command_buffers_writable_.back();
    if (dfn.vkResetCommandPool(device, command_buffer.pool, 0) != VK_SUCCESS) {"""
end_marker = """    uint64_t submission_current = GetCurrentSubmission();
    current_submission_wait_stage_masks_.clear();"""
a = s.index(start_marker); b = s.index(end_marker)
assert s.count(start_marker) == 1 and s.count(end_marker) == 1 and a < b
original_block = s[a:b]
assert "deferred_command_buffer_.Execute(command_buffer.buffer);" in original_block
assert "upload_command_buffer_.Execute(command_buffer.buffer);" in original_block
new_block = """    assert_false(command_buffers_writable_.empty());
    CommandBuffer command_buffer = command_buffers_writable_.back();
    assert_false(fences_free_.empty());
    VkFence fence = fences_free_.back();
    const bool perf_upload = upload_command_buffer_used_ && perf_query_pool_ != VK_NULL_HANDLE &&
                             perf_block_used_ < kPerfBlock;
    const uint32_t perf_upload_begin = perf_block_base_ + kPerfBlockDeferred;
    // ht_async_submit: mid-frame submissions go to the worker thread. Swap submissions (the
    // presenter submits right after the guest output refresh) stay synchronous.
    if (ht_async_submit_enabled_ && !is_swap && !ht_submit_failed_.load()) {
      if (dfn.vkResetFences(device, 1, &fence) != VK_SUCCESS) {
        REXGPU_ERROR("Failed to reset a Vulkan submission fence");
        return false;
      }
      HtSubmitJob job;
      job.command_buffer = command_buffer;
      job.fence = fence;
      job.wait_semaphores = current_submission_wait_semaphores_;
      job.wait_stages = current_submission_wait_stage_masks_;
      job.upload_used = upload_command_buffer_used_;
      job.perf_upload = perf_upload;
      job.perf_upload_begin = perf_upload_begin;
      {
        std::lock_guard<std::mutex> lock(ht_submit_mutex_);
        for (std::vector<uintmax_t>* stream : {&job.main_stream, &job.upload_stream}) {
          if (!ht_stream_free_.empty()) {
            *stream = std::move(ht_stream_free_.back());
            ht_stream_free_.pop_back();
          }
          stream->clear();
        }
      }
      deferred_command_buffer_.HtSwapStream(job.main_stream);
      if (upload_command_buffer_used_) {
        upload_command_buffer_.HtSwapStream(job.upload_stream);
      }
      if (REXCVAR_GET(ht_async_submit_verify)) {
        job.hash = HtStreamHash(job.upload_stream, job.main_stream);
        if (REXCVAR_GET(ht_async_submit_verify_break)) {
          job.hash ^= 1;
        }
      }
      {
        std::lock_guard<std::mutex> lock(ht_submit_mutex_);
        ht_submit_queue_.push_back(std::move(job));
        ++ht_submit_pending_;
      }
      ht_submit_cv_.notify_one();
      ++ht_async_jobs_;
    } else {
      if (ht_async_submit_enabled_) {
        HtFlushSubmitWorker();
        ++ht_sync_submits_;
        if (ht_submit_failed_.load()) {
          return false;
        }
      }
      VkResult submit_result = VK_SUCCESS;
      if (!HtRecordAndSubmit(command_buffer, fence, true, current_submission_wait_semaphores_,
                             current_submission_wait_stage_masks_, upload_command_buffer_used_,
                             upload_command_buffer_.HtData(), upload_command_buffer_.HtSize(),
                             deferred_command_buffer_.HtData(), deferred_command_buffer_.HtSize(),
                             perf_upload, perf_upload_begin, &submit_result)) {
        if (submit_result == VK_ERROR_DEVICE_LOST && !device_lost_) {
          device_lost_ = true;
          if (graphics_system_) {
            graphics_system_->OnHostGpuLossFromAnyThread(true);
          }
        }
        return false;
      }
    }
    if (perf_upload) {
      perf_pending_.push_back(
          {perf_upload_begin, perf_upload_begin + 1, PerfGpu::kUpload, GetCurrentSubmission()});
    }
    if (ht_async_submit_enabled_) {
      const auto ht_now = std::chrono::steady_clock::now();
      if (ht_now - ht_async_log_time_ > std::chrono::seconds(10)) {
        ht_async_log_time_ = ht_now;
        REXGPU_WARN("[ht-async-submit] async={} sync={} flushes={} verified={} mismatches={}",
                    ht_async_jobs_, ht_sync_submits_, ht_async_flushes_,
                    ht_async_verified_.load(), ht_async_mismatches_.load());
      }
    }
"""
s = s[:a] + new_block + s[b:]

# The shared recording + submit function = the original block, parameterized.
func = """
bool VulkanCommandProcessor::HtRecordAndSubmit(
    const CommandBuffer& command_buffer, VkFence fence, bool reset_fence,
    const std::vector<VkSemaphore>& wait_semaphores,
    const std::vector<VkPipelineStageFlags>& wait_stages, bool upload_used,
    const uintmax_t* upload_data, size_t upload_size, const uintmax_t* main_data,
    size_t main_size, bool perf_upload, uint32_t perf_upload_begin, VkResult* result_out) {
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  *result_out = VK_SUCCESS;
  if (dfn.vkResetCommandPool(device, command_buffer.pool, 0) != VK_SUCCESS) {
    REXGPU_ERROR("Failed to reset a Vulkan command pool");
    return false;
  }
  VkCommandBufferBeginInfo command_buffer_begin_info;
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = nullptr;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  command_buffer_begin_info.pInheritanceInfo = nullptr;
  if (dfn.vkBeginCommandBuffer(command_buffer.buffer, &command_buffer_begin_info) != VK_SUCCESS) {
    REXGPU_ERROR("Failed to begin a Vulkan command buffer");
    return false;
  }
  if (upload_used) {
    // Hoisted guest memory uploads: wait for every earlier use of the
    // shared memory (reads or GPU writes from previous submissions), copy,
    // then make the copies visible to everything recorded in this
    // submission.
    VkMemoryBarrier upload_barrier;
    upload_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    upload_barrier.pNext = nullptr;
    upload_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    upload_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    if (perf_upload) {
      dfn.vkCmdResetQueryPool(command_buffer.buffer, perf_query_pool_, perf_upload_begin, 2);
    }
    dfn.vkCmdPipelineBarrier(command_buffer.buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &upload_barrier, 0, nullptr,
                             0, nullptr);
    if (perf_upload) {
      // After the barrier: measures only the copies, not the drain of the
      // previous submission.
      dfn.vkCmdWriteTimestamp(command_buffer.buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              perf_query_pool_, perf_upload_begin);
    }
    upload_command_buffer_.ExecuteStream(command_buffer.buffer, upload_data, upload_size);
    upload_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    upload_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    dfn.vkCmdPipelineBarrier(command_buffer.buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &upload_barrier, 0,
                             nullptr, 0, nullptr);
    if (perf_upload) {
      dfn.vkCmdWriteTimestamp(command_buffer.buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              perf_query_pool_, perf_upload_begin + 1);
    }
  }
  deferred_command_buffer_.ExecuteStream(command_buffer.buffer, main_data, main_size);
  if (dfn.vkEndCommandBuffer(command_buffer.buffer) != VK_SUCCESS) {
    REXGPU_ERROR("Failed to end a Vulkan command buffer");
    return false;
  }

  VkSubmitInfo submit_info;
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pNext = nullptr;
  if (!wait_semaphores.empty()) {
    submit_info.waitSemaphoreCount = uint32_t(wait_semaphores.size());
    submit_info.pWaitSemaphores = wait_semaphores.data();
    submit_info.pWaitDstStageMask = wait_stages.data();
  } else {
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;
  }
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer.buffer;
  submit_info.signalSemaphoreCount = 0;
  submit_info.pSignalSemaphores = nullptr;
  if (reset_fence && dfn.vkResetFences(device, 1, &fence) != VK_SUCCESS) {
    REXGPU_ERROR("Failed to reset a Vulkan submission fence");
    return false;
  }
  VkResult submit_result;
  {
    ui::vulkan::VulkanDevice::Queue::Acquisition queue_acquisition =
        vulkan_device->AcquireQueue(vulkan_device->queue_family_graphics_compute(), 0);
    submit_result = dfn.vkQueueSubmit(queue_acquisition.queue(), 1, &submit_info, fence);
  }
  *result_out = submit_result;
  if (submit_result != VK_SUCCESS) {
    REXGPU_ERROR("Failed to submit a Vulkan command buffer");
    return false;
  }
  return true;
}

void VulkanCommandProcessor::HtSubmitWorkerMain() {
  for (;;) {
    HtSubmitJob job;
    {
      std::unique_lock<std::mutex> lock(ht_submit_mutex_);
      ht_submit_cv_.wait(lock, [this]() { return ht_submit_stop_ || !ht_submit_queue_.empty(); });
      if (ht_submit_queue_.empty()) {
        return;
      }
      job = std::move(ht_submit_queue_.front());
      ht_submit_queue_.pop_front();
    }
    if (!ht_submit_failed_.load()) {
      VkResult result = VK_SUCCESS;
      bool ok = HtRecordAndSubmit(job.command_buffer, job.fence, false, job.wait_semaphores,
                                  job.wait_stages, job.upload_used, job.upload_stream.data(),
                                  job.upload_stream.size(), job.main_stream.data(),
                                  job.main_stream.size(), job.perf_upload, job.perf_upload_begin,
                                  &result);
      if (job.hash) {
        ++ht_async_verified_;
        if (HtStreamHash(job.upload_stream, job.main_stream) != job.hash) {
          ++ht_async_mismatches_;
        }
      }
      if (!ok) {
        REXGPU_ERROR("[ht-async-submit] worker submission failed ({}); treating as device loss",
                     int(result));
        ht_submit_failed_ = true;
      }
    }
    {
      std::lock_guard<std::mutex> lock(ht_submit_mutex_);
      job.main_stream.clear();
      job.upload_stream.clear();
      ht_stream_free_.push_back(std::move(job.main_stream));
      ht_stream_free_.push_back(std::move(job.upload_stream));
      --ht_submit_pending_;
    }
    ht_submit_done_cv_.notify_all();
  }
}

void VulkanCommandProcessor::HtFlushSubmitWorker() {
  if (!ht_submit_thread_.joinable()) {
    return;
  }
  std::unique_lock<std::mutex> lock(ht_submit_mutex_);
  if (ht_submit_pending_) {
    ++ht_async_flushes_;
  }
  ht_submit_done_cv_.wait(lock, [this]() { return ht_submit_pending_ == 0; });
}

size_t VulkanCommandProcessor::HtUnsubmittedJobs() {
  std::lock_guard<std::mutex> lock(ht_submit_mutex_);
  return ht_submit_pending_;
}

void VulkanCommandProcessor::HtStopSubmitWorker() {
  if (!ht_submit_thread_.joinable()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(ht_submit_mutex_);
    ht_submit_stop_ = true;
  }
  ht_submit_cv_.notify_all();
  ht_submit_thread_.join();
}

uint64_t VulkanCommandProcessor::HtStreamHash(const std::vector<uintmax_t>& a,
                                              const std::vector<uintmax_t>& b) {
  uint64_t h = XXH3_64bits(a.data(), a.size() * sizeof(uintmax_t)) * 31 ^
               XXH3_64bits(b.data(), b.size() * sizeof(uintmax_t));
  return h ? h : 1;
}
"""
anchor = "void VulkanCommandProcessor::ClearTransientDescriptorPools() {"
assert s.count(anchor) == 1
s = s.replace(anchor, func.lstrip("\n") + "\n" + anchor)
p.write_text(s)
print("patched async submit")
