"""Experimental ZPD queries inside render passes. Default OFF until GPU proof.
Apply once to the 8.9.2 SDK. Keeps the original path under ht_zpd_batch=false.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])
h = root / 'include/rex/graphics/vulkan/command_processor.h'
c = root / 'src/graphics/vulkan/command_processor.cpp'
s = c.read_text()
t = h.read_text()

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:100])
    s = s.replace(old, new)

t = t.replace('  void ZpdResolve();', '''  void ZpdResolve();
  void ZpdBatchPrepare();
  void ZpdBatchBeginSegment();
  void ZpdBatchEndSegment();
  void ZpdBatchCopy();
  void ZpdBatchClose(uint32_t key);
  void ZpdBatchSnapshot();''')
t = t.replace('    uint64_t estimate;\n  };\n  std::deque<ZpdInterval>', '''    uint64_t estimate;
    std::vector<uint32_t> parts;
    uint64_t captured_samples = 0;
    bool captured = false;
  };
  static constexpr uint32_t kZpdBatchSize = 256;
  std::array<uint64_t, kMaxOcclusionQueries / kZpdBatchSize> zpd_batch_submission_{};
  uint32_t zpd_batch_remaining_ = 0;
  bool zpd_batch_interval_open_ = false;
  bool zpd_batch_segment_active_ = false;
  uint32_t zpd_batch_segment_ = 0;
  std::vector<uint32_t> zpd_batch_parts_;
  std::vector<uint32_t> zpd_batch_copies_;
  std::deque<ZpdInterval>''')
assert 'std::vector<uint32_t> parts;' in t
rep('REXCVAR_DEFINE_BOOL(ht_zpd_exact_key, true,', '''REXCVAR_DEFINE_BOOL(ht_zpd_batch, false, "GPU/Vulkan",
                    "Experimental: batch query reset/copy and keep ZPD events inside render passes");
REXCVAR_DEFINE_BOOL(ht_zpd_exact_key, true,''')
rep('REXCVAR_DEFINE_BOOL(ht_zpd_batch, false,', '''REXCVAR_DEFINE_BOOL(ht_zpd_batch_trace, false, "GPU/Vulkan",
                    "Diagnostic interval sample trace (slow, not for benchmarking)");
REXCVAR_DEFINE_BOOL(ht_zpd_batch, false,''')
rep('''    EndRenderPass();
    // Close the interval measured since the previous event;''', '''    if (REXCVAR_GET(ht_zpd_batch) && GetVulkanDevice()->properties().occlusionQueryPrecise) {
      ZpdBatchClose(REXCVAR_GET(ht_zpd_exact_key) ? report_address : (report_address & 0xFFF));
      zpd_batch_interval_open_ = true;
      if (in_render_pass_ && !zpd_batch_remaining_) {
        // Rare exhaustion: continue in the next naturally opened pass.
        EndRenderPass();
      }
      if (in_render_pass_) ZpdBatchBeginSegment();
    } else {
    EndRenderPass();
    // Close the interval measured since the previous event;''')
rep('''      active_occlusion_query_.valid = true;
    }
  }

  WriteGuestOcclusionResult''', '''      active_occlusion_query_.valid = true;
    }
    }
  }

  WriteGuestOcclusionResult''')
rep('''    const uint64_t samples = NormalizeOcclusionSamples(results[interval.host_index]);''', '''    uint64_t raw_samples = interval.captured_samples;
    if (interval.captured) {
      // A recycled block was snapshotted without updating guest estimates.
    } else if (interval.parts.empty()) {
      raw_samples = results[interval.host_index];
    } else {
      for (uint32_t part : interval.parts) raw_samples += results[part];
    }
    const uint64_t samples = NormalizeOcclusionSamples(raw_samples);
    if (REXCVAR_GET(ht_perf_log) && interval.key != UINT32_MAX) {
      static uint64_t n = 0, leak = 0, miss = 0, parts = 0;
      ++n;
      leak += interval.estimate > 0 && samples == 0;
      miss += interval.estimate == 0 && samples > 0;
      parts += interval.parts.size();
      if (n % 4000 == 0) REXGPU_WARN("[ht-zpd-batch-stats] intervals={} leak={} miss={} parts={}", n, leak, miss, parts);
    }
    if (REXCVAR_GET(ht_zpd_batch_trace)) {
      REXGPU_WARN("[ht-zpd-batch-result] key={:08X} raw={} samples={} parts={} estimate={}",
          interval.key, raw_samples, samples, interval.parts.size(), interval.estimate);
    }''')
# Empty optimized intervals have no GPU query; represent them with a sentinel.
rep('''      raw_samples = results[interval.host_index];''', '''      raw_samples = interval.host_index == UINT32_MAX ? 0 : results[interval.host_index];''')

methods = r'''
// Reset a fresh block only outside rendering. Before recycling, wait for its
// last submission and consume CPU readbacks, so results cannot be overwritten.
void VulkanCommandProcessor::ZpdBatchPrepare() {
  if (!REXCVAR_GET(ht_zpd_batch) || !GetVulkanDevice()->properties().occlusionQueryPrecise || !occlusion_query_resources_available_ ||
      zpd_batch_remaining_) return;
  assert_false(in_render_pass_);
  const uint32_t base = (occlusion_query_cursor_ + kZpdBatchSize - 1) & ~(kZpdBatchSize - 1);
  occlusion_query_cursor_ = base % kMaxOcclusionQueries;
  const uint32_t block = occlusion_query_cursor_ / kZpdBatchSize;
  const uint64_t previous = zpd_batch_submission_[block];
  if (previous == GetCurrentSubmission()) {
    // More than a pool of queries in one submission: close it before reuse.
    EndSubmission(false);
    BeginSubmission(true);
  }
  CheckSubmissionFenceAndDeviceLoss(previous);
  if (device_lost_ || GetCompletedSubmission() < previous) return;
  if (previous) ZpdBatchSnapshot();
  deferred_command_buffer_.CmdVkResetQueryPool(occlusion_query_pool_, occlusion_query_cursor_, kZpdBatchSize);
  zpd_batch_remaining_ = kZpdBatchSize;
}

void VulkanCommandProcessor::ZpdBatchBeginSegment() {
  if (!REXCVAR_GET(ht_zpd_batch) || !zpd_batch_interval_open_ ||
      zpd_batch_segment_active_ || !zpd_batch_remaining_) return;
  assert_true(in_render_pass_);
  const uint32_t index = occlusion_query_cursor_++;
  --zpd_batch_remaining_;
  zpd_batch_submission_[index / kZpdBatchSize] = GetCurrentSubmission();
  deferred_command_buffer_.CmdVkBeginQuery(occlusion_query_pool_, index,
      GetVulkanDevice()->properties().occlusionQueryPrecise ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
  zpd_batch_segment_ = index;
  zpd_batch_segment_active_ = true;
}

void VulkanCommandProcessor::ZpdBatchEndSegment() {
  if (!zpd_batch_segment_active_) return;
  assert_true(in_render_pass_);
  deferred_command_buffer_.CmdVkEndQuery(occlusion_query_pool_, zpd_batch_segment_);
  zpd_batch_parts_.push_back(zpd_batch_segment_);
  zpd_batch_copies_.push_back(zpd_batch_segment_);
  zpd_batch_segment_active_ = false;
}

void VulkanCommandProcessor::ZpdBatchCopy() {
  assert_false(in_render_pass_);
  // Parts are consecutive except at pool wrap. Coalesce copies too.
  for (size_t i = 0; i < zpd_batch_copies_.size();) {
    const uint32_t first = zpd_batch_copies_[i++];
    uint32_t count = 1;
    while (i < zpd_batch_copies_.size() && zpd_batch_copies_[i] == first + count) { ++i; ++count; }
    deferred_command_buffer_.CmdVkCopyQueryPoolResults(occlusion_query_pool_, first, count,
        occlusion_query_readback_buffer_, sizeof(uint64_t) * first, sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
  }
  zpd_batch_copies_.clear();
}

void VulkanCommandProcessor::ZpdBatchClose(uint32_t key) {
  if (!zpd_batch_interval_open_) return;
  ZpdBatchEndSegment();
  uint64_t estimate = 0;
  if (key != UINT32_MAX) {
    auto exact = ht_zpd_address_estimate_.find(key);
    auto slot = zpd_interval_estimate_.find(key & 0xFFF);
    if (key > 0xFFF && exact != ht_zpd_address_estimate_.end()) estimate = exact->second;
    else if (slot != zpd_interval_estimate_.end()) estimate = slot->second;
  }
  zpd_counter_estimate_ += estimate;
  const uint32_t first = zpd_batch_parts_.empty() ? UINT32_MAX : zpd_batch_parts_.front();
  zpd_intervals_.push_back({first, GetCurrentSubmission(), key, estimate, std::move(zpd_batch_parts_)});
  zpd_batch_parts_.clear();
  zpd_batch_interval_open_ = false;
}

void VulkanCommandProcessor::ZpdBatchSnapshot() {
  // Snapshot completed intervals before block reuse, without changing guest
  // estimates until the original frame-boundary resolver.
  const auto* device = GetVulkanDevice();
  if (!(device->memory_types().host_coherent & (uint32_t(1) << occlusion_query_readback_memory_type_))) {
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = occlusion_query_readback_memory_;
    range.size = std::min(rex::round_up(VkDeviceSize(sizeof(uint64_t) * kMaxOcclusionQueries),
        device->properties().nonCoherentAtomSize), occlusion_query_readback_memory_size_);
    device->functions().vkInvalidateMappedMemoryRanges(device->device(), 1, &range);
  }
  const uint64_t* values = reinterpret_cast<const uint64_t*>(occlusion_query_readback_mapping_);
  for (auto& interval : zpd_intervals_) {
    if (interval.submission > GetCompletedSubmission()) break;
    if (interval.captured) continue;
    if (interval.parts.empty() && interval.host_index != UINT32_MAX) {
      interval.captured_samples = values[interval.host_index];
    } else {
      for (uint32_t part : interval.parts) interval.captured_samples += values[part];
    }
    interval.captured = true;
  }
}

'''
rep('void VulkanCommandProcessor::ZpdResolve() {', methods + 'void VulkanCommandProcessor::ZpdResolve() {')
# Both cache pass entry functions must end segments before changing targets.
old = '''  if (in_render_pass_) {
    if (use_dynamic_rendering) {'''
assert s.count(old) == 2
s = s.replace(old, '''  if (in_render_pass_) {
    ZpdBatchEndSegment();
    if (use_dynamic_rendering) {''')
old = '''    in_render_pass_ = false;
  }

  current_render_pass_ = use_dynamic_rendering'''
assert s.count(old) == 2
s = s.replace(old, '''    in_render_pass_ = false;
  }
  ZpdBatchPrepare();

  current_render_pass_ = use_dynamic_rendering''')
old = '  in_render_pass_ = true;\n}'
assert s.count(old) == 2
s = s.replace(old, '  in_render_pass_ = true;\n  ZpdBatchBeginSegment();\n}')
rep('''  if (current_render_pass_ == VK_NULL_HANDLE) {
    deferred_command_buffer_.CmdVkEndRendering();''', '''  ZpdBatchEndSegment();
  if (current_render_pass_ == VK_NULL_HANDLE) {
    deferred_command_buffer_.CmdVkEndRendering();''')
# Presentation gamma is a direct render pass, outside the cache helpers.
rep('''          SwapFramebuffer& swap_framebuffer = swap_framebuffers_[swap_framebuffer_index];''', '''          ZpdBatchPrepare();
          SwapFramebuffer& swap_framebuffer = swap_framebuffers_[swap_framebuffer_index];''')
rep('''          deferred_command_buffer_.CmdVkDraw(3, 1, 0, 0);
          deferred_command_buffer_.CmdVkEndRenderPass();''', '''          in_render_pass_ = true;
          ZpdBatchBeginSegment();
          deferred_command_buffer_.CmdVkDraw(3, 1, 0, 0);
          ZpdBatchEndSegment();
          deferred_command_buffer_.CmdVkEndRenderPass();
          in_render_pass_ = false;''')
rep('''    PerfGpuEnd(PerfGpu::kSubmission);
    ++perf_counters_.submissions;''', '''    // Include deferred query copies in the measured GPU submission span.
    if (REXCVAR_GET(ht_zpd_batch)) {
      ZpdBatchClose(UINT32_MAX);
      ZpdBatchCopy();
    }
    PerfGpuEnd(PerfGpu::kSubmission);
    ++perf_counters_.submissions;''')
h.write_text(t)
c.write_text(s)
print('Experimental ht_zpd_batch applied (default OFF)')
