"""Measurement only (temporary): time the render target dump of resolves under
the texcopy= category (unused since ht_texture_direct), to split resolve= into
dump and copy. Revert afterwards (apply_measure_dump.sh keeps a backup).
usage: python3 patch_measure_dump.py /root/ht/sdk"""
import sys
from pathlib import Path
p = Path(sys.argv[1]) / "src/graphics/vulkan/render_target_cache.cpp"
s = p.read_text()
old = """          if (!DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch)) {
            REXGPU_ERROR("VulkanRenderTargetCache: Failed to dump host render targets for resolve");"""
assert s.count(old) == 1
s = s.replace(old, """          command_processor_.PerfGpuBegin(VulkanCommandProcessor::PerfGpu::kTexCopy);
          const bool ht_dumped = DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch);
          command_processor_.PerfGpuEnd(VulkanCommandProcessor::PerfGpu::kTexCopy);
          if (!ht_dumped) {
            REXGPU_ERROR("VulkanRenderTargetCache: Failed to dump host render targets for resolve");""")
old2 = "  return DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch);\n}"
assert s.count(old2) == 1
s = s.replace(old2, """  command_processor_.PerfGpuBegin(VulkanCommandProcessor::PerfGpu::kTexCopy);
  const bool ht_dumped = DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch);
  command_processor_.PerfGpuEnd(VulkanCommandProcessor::PerfGpu::kTexCopy);
  return ht_dumped;
}""")
p.write_text(s)
print("patched")
