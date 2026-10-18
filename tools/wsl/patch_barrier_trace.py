"""Measurement only: in the traced frame (HT_TRACE_FRAME), log every pipeline barrier
batch that ends a render pass, with its stages and buffer / image barriers
([ht-trace] rpbreak ...). Also logs barrier batches outside render passes
([ht-trace] barrier ...).

usage: python3 patch_barrier_trace.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])
cp = root / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
old = """  if (in_render_pass_) ++perf_counters_.rp_breaks_barrier;
  EndRenderPass();"""
new = """  if (ht_trace_active) {
    for (const VkBufferMemoryBarrier& b : pending_barriers_buffer_memory_barriers_) {
      REXGPU_WARN("[ht-trace]   {} after draw {} buf {:p} off={:X} size={:X} access {:X}->{:X}",
                  in_render_pass_ ? "rpbreak" : "barrier", uint64_t(ht_trace_draw),
                  static_cast<const void*>(b.buffer), uint64_t(b.offset), uint64_t(b.size),
                  b.srcAccessMask, b.dstAccessMask);
    }
    for (const VkImageMemoryBarrier& b : pending_barriers_image_memory_barriers_) {
      REXGPU_WARN("[ht-trace]   {} after draw {} img {:p} aspect={:X} layout {}->{} access {:X}->{:X}",
                  in_render_pass_ ? "rpbreak" : "barrier", uint64_t(ht_trace_draw),
                  static_cast<const void*>(b.image), b.subresourceRange.aspectMask,
                  uint32_t(b.oldLayout), uint32_t(b.newLayout), b.srcAccessMask, b.dstAccessMask);
    }
    for (const auto& p : pending_barriers_) {
      REXGPU_WARN("[ht-trace]   {} after draw {} stages {:X}->{:X}",
                  in_render_pass_ ? "rpbreak" : "barrier", uint64_t(ht_trace_draw),
                  p.src_stage_mask, p.dst_stage_mask);
    }
  }
  if (in_render_pass_) ++perf_counters_.rp_breaks_barrier;
  EndRenderPass();"""
assert s.count(old) == 1
s = s.replace(old, new)
old2 = """  // Draw.
  if (ht_tq_reset_ && ht_trace_active"""
new2 = """  // Draw.
  if (ht_trace_active) {
    REXGPU_WARN("[ht-trace]   shaders draw {} vs=0x{:016X} ps=0x{:016X} vtx={}",
                uint64_t(ht_trace_draw), vertex_shader->ucode_data_hash(),
                pixel_shader ? pixel_shader->ucode_data_hash() : 0,
                primitive_processing_result.host_draw_vertex_count);
  }
  if (ht_tq_reset_ && ht_trace_active"""
assert s.count(old2) == 1, "apply patch_draw_timing.py first"
s = s.replace(old2, new2)
cp.write_text(s)
print("patched barrier trace")
