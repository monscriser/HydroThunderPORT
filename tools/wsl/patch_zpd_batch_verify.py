"""Diagnostic exact GPU proof for Hydro's read-only occlusion sphere draws.
Apply after patch_zpd_batch_experiment.py. All switches default OFF.
Replay is guarded: host render targets, no pixel shader, known sphere VS,
no color/depth/stencil or memexport writes. Reference query starts/ends OUTSIDE
rendering, as in 8.9.2; candidate query starts/ends INSIDE. Same GPU inputs.
"""
from pathlib import Path
import sys
r=Path(sys.argv[1])
h=r/'include/rex/graphics/vulkan/command_processor.h'
c=r/'src/graphics/vulkan/command_processor.cpp'
t=h.read_text();s=c.read_text()
def rep(a,b):
    global s
    assert s.count(a)==1,(s.count(a),a[:100])
    s=s.replace(a,b)
t=t.replace('  void ZpdBatchSnapshot();','''  void ZpdBatchSnapshot();
  void ZpdBatchVerifyResolve();''')
t=t.replace('  bool zpd_batch_segment_active_ = false;','''  bool zpd_batch_segment_active_ = false;
  bool zpd_batch_verify_suspended_ = false;
  struct ZpdVerifyPair { uint32_t candidate, reference; uint64_t submission; };
  std::deque<ZpdVerifyPair> zpd_batch_verify_pairs_;
  uint64_t zpd_batch_verified_ = 0, zpd_batch_verify_nonzero_ = 0, zpd_batch_verify_mismatches_ = 0;''')
rep('REXCVAR_DEFINE_BOOL(ht_zpd_batch_trace, false,', '''REXCVAR_DEFINE_BOOL(ht_zpd_batch_verify, false, "GPU/Vulkan",
                    "TEST ONLY: replay read-only light tests with original outside-render query");
REXCVAR_DEFINE_BOOL(ht_zpd_batch_verify_break, false, "GPU/Vulkan",
                    "TEST ONLY: perturb reference result to prove the GPU comparator detects errors");
REXCVAR_DEFINE_BOOL(ht_zpd_batch_trace, false,''')
rep('''      zpd_batch_segment_active_ || !zpd_batch_remaining_) return;''','''      zpd_batch_segment_active_ || zpd_batch_verify_suspended_ || !zpd_batch_remaining_) return;''')
rep('''  const uint64_t* values = reinterpret_cast<const uint64_t*>(occlusion_query_readback_mapping_);
  for (auto& interval''','''  const uint64_t* values = reinterpret_cast<const uint64_t*>(occlusion_query_readback_mapping_);
  ZpdBatchVerifyResolve();
  for (auto& interval''')
rep('''  const uint64_t* results = reinterpret_cast<const uint64_t*>(occlusion_query_readback_mapping_);
  while (!zpd_intervals_''','''  const uint64_t* results = reinterpret_cast<const uint64_t*>(occlusion_query_readback_mapping_);
  ZpdBatchVerifyResolve();
  while (!zpd_intervals_''')
rep('void VulkanCommandProcessor::ZpdResolve() {',r'''
void VulkanCommandProcessor::ZpdBatchVerifyResolve() {
  const auto* results = reinterpret_cast<const uint64_t*>(occlusion_query_readback_mapping_);
  while (!zpd_batch_verify_pairs_.empty() &&
         zpd_batch_verify_pairs_.front().submission <= GetCompletedSubmission()) {
    const auto pair = zpd_batch_verify_pairs_.front();
    const uint64_t candidate = results[pair.candidate];
    const uint64_t reference = results[pair.reference] + (REXCVAR_GET(ht_zpd_batch_verify_break) ? 1 : 0);
    ++zpd_batch_verified_;
    zpd_batch_verify_nonzero_ += candidate > 0;
    zpd_batch_verify_mismatches_ += candidate != reference;
    if (zpd_batch_verified_ % 128 == 0) {
      REXGPU_WARN("[ht-zpd-pair-verify] compared={} nonzero={} mismatches={}",
          zpd_batch_verified_, zpd_batch_verify_nonzero_, zpd_batch_verify_mismatches_);
    }
    zpd_batch_verify_pairs_.pop_front();
  }
}

void VulkanCommandProcessor::ZpdResolve() {''')
anchor='''  // Draw.
  if (primitive_processing_result.index_buffer_type =='''
rep(anchor,r'''  // Diagnostic same-input proof. The replay is allowed ONLY for Hydro's
  // read-only sphere: no pixel shader, color, depth, stencil or memexport writes.
  uint32_t ht_zpd_verify_original = UINT32_MAX;
  if (REXCVAR_GET(ht_zpd_batch_verify) && REXCVAR_GET(ht_zpd_batch) &&
      vertex_shader->ucode_data_hash() == UINT64_C(0x72F241126568C627) && !pixel_shader) {
    static uint32_t guard_logs = 0;
    if (guard_logs++ < 8) REXGPU_WARN("[ht-zpd-pair-guard] depth={:08X} color={} memexport={} host={} open={} active={} remaining={}",
        normalized_depth_control.value, normalized_color_mask, memexport_writes_possible,
        render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets,
        zpd_batch_interval_open_, zpd_batch_segment_active_, zpd_batch_remaining_);
    if (!normalized_color_mask && !normalized_depth_control.z_write_enable &&
        !normalized_depth_control.stencil_enable && !memexport_writes_possible &&
        render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets &&
        zpd_batch_interval_open_ && zpd_batch_segment_active_ && zpd_batch_remaining_ >= 3) {
      ZpdBatchEndSegment();
      ZpdBatchBeginSegment();
      ht_zpd_verify_original = zpd_batch_segment_;
    }
  }

  // Draw.
  if (primitive_processing_result.index_buffer_type ==''')
rep('''  // Invalidate textures in memexported memory and watch for changes.
''',r'''  if (ht_zpd_verify_original != UINT32_MAX) {
    ZpdBatchEndSegment();
    EndRenderPass();
    zpd_batch_verify_suspended_ = true;
    const uint32_t reference = occlusion_query_cursor_++;
    --zpd_batch_remaining_;
    zpd_batch_submission_[reference / kZpdBatchSize] = GetCurrentSubmission();
    deferred_command_buffer_.CmdVkBeginQuery(occlusion_query_pool_, reference, VK_QUERY_CONTROL_PRECISE_BIT);
    SubmitBarriersAndEnterRenderTargetCacheRenderPass(
        render_target_cache_->last_update_render_pass(), render_target_cache_->last_update_framebuffer());
    // The original index binding, pipeline, constants, viewport and depth input
    // remain unchanged. Neither the first draw nor this replay writes anything.
    if (primitive_processing_result.index_buffer_type == PrimitiveProcessor::ProcessedIndexBufferType::kNone || shader_32bit_index_dma) {
      deferred_command_buffer_.CmdVkDraw(primitive_processing_result.host_draw_vertex_count, 1, 0, 0);
    } else {
      deferred_command_buffer_.CmdVkDrawIndexed(primitive_processing_result.host_draw_vertex_count, 1, 0, 0, 0);
    }
    EndRenderPass();
    deferred_command_buffer_.CmdVkEndQuery(occlusion_query_pool_, reference);
    zpd_batch_copies_.push_back(reference);
    zpd_batch_verify_pairs_.push_back({ht_zpd_verify_original, reference, GetCurrentSubmission()});
    zpd_batch_verify_suspended_ = false;
    SubmitBarriersAndEnterRenderTargetCacheRenderPass(
        render_target_cache_->last_update_render_pass(), render_target_cache_->last_update_framebuffer());
  }

  // Invalidate textures in memexported memory and watch for changes.
''')
h.write_text(t);c.write_text(s)
print('ZPD exact GPU paired proof added (diagnostic, default OFF)')
