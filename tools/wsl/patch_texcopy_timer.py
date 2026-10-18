"""Splits the GPU time of texture loads: the compute conversion stays in
texload=, the final vkCmdCopyBufferToImage goes to a new texcopy= ([ht-perf]).

usage: python3 patch_texcopy_timer.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])
h = root / "include/rex/graphics/vulkan/command_processor.h"
s = h.read_text()
if "kTexCopy" not in s:
    s = s.replace("kZpd, kUpload, kCount };", "kZpd, kUpload, kTexCopy, kCount };", 1)
    h.write_text(s)

cp = root / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
if "texcopy=" not in s:
    s = s.replace('texload={:.2f} upload={:.2f} |', 'texload={:.2f} texcopy={:.2f} upload={:.2f} |', 1)
    s = s.replace("perf_gpu_ms_[size_t(PerfGpu::kTextureLoad)] / f,\n",
                  "perf_gpu_ms_[size_t(PerfGpu::kTextureLoad)] / f, perf_gpu_ms_[size_t(PerfGpu::kTexCopy)] / f,\n", 1)
    assert "kTexCopy)] / f" in s
    cp.write_text(s)

tc = root / "src/graphics/vulkan/texture_cache.cpp"
s = tc.read_text()
if "PerfGpu::kTexCopy" not in s:
    anchor = "  // Submit copying from the copy buffer to the host texture.\n"
    assert anchor in s
    s = s.replace(anchor, "  command_processor_.PerfGpuEnd(VulkanCommandProcessor::PerfGpu::kTextureLoad);\n"
                          "  command_processor_.PerfGpuBegin(VulkanCommandProcessor::PerfGpu::kTexCopy);\n" + anchor, 1)
    tail = "    copy_region.imageExtent.depth = std::max(depth >> level, UINT32_C(1));\n  }\n\n  return true;\n}\n"
    assert tail in s
    s = s.replace(tail, "    copy_region.imageExtent.depth = std::max(depth >> level, UINT32_C(1));\n  }\n"
                        "  command_processor_.PerfGpuEnd(VulkanCommandProcessor::PerfGpu::kTexCopy);\n\n  return true;\n}\n", 1)
    tc.write_text(s)
print("patched")
