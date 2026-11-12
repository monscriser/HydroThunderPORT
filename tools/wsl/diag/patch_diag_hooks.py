#!/usr/bin/env python3
"""DIAGNOSTIC BUILD ONLY: hooks the per-track telemetry (app/src/ht_diag.h) needs.

1. rex::ht_diag::SetFileOpenObserver - called with every guest file path the VFS
   opens. Each track opens its own sound bank (Audio/Xbox360/<Track>.fsb; DLC
   Pack01TrackNN.fsb), which is how the telemetry knows which track is loaded.
2. rex::ht_diag::SetSwapObserver - called once per guest frame (top of
   VulkanCommandProcessor::PerfOnSwap), for exact per-frame frame times.
3. [ht-vram] line in the 2 s [ht-perf] report: VK_EXT_memory_budget usage/budget
   per memory heap (device-local = VRAM; on the Steam Deck APU also the GTT heap).

Both observers are plain atomic function pointers, null by default: with no
observer the cost is one relaxed atomic load per file open / frame.
usage: patch_diag_hooks.py <sdk root>
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
MARK = "ht_diag hooks"


def patch(rel, pairs):
    f = root / rel
    s = f.read_text()
    if MARK in s:
        print(f"{rel}: already patched")
        return
    for old, new in pairs:
        assert s.count(old) == 1, f"{rel}: anchor not unique/missing: {old[:60]!r}"
        s = s.replace(old, new)
    f.write_text(s)
    print(f"{rel}: patched")


(root / "include/rex/ht_diag_hooks.h").write_text(
    "// " + MARK + " (diagnostic build). Defined in src/filesystem/virtual_file_system.cpp.\n"
    "#pragma once\n#include <string_view>\n\nnamespace rex::ht_diag {\n"
    "using FileOpenObserver = void (*)(std::string_view guest_path);\n"
    "using SwapObserver = void (*)();\n"
    "void SetFileOpenObserver(FileOpenObserver observer);\n"
    "FileOpenObserver GetFileOpenObserver();\n"
    "void SetSwapObserver(SwapObserver observer);\n"
    "SwapObserver GetSwapObserver();\n"
    "// Ask the GPU command thread to trace the next frame (HT_TRACE_FRAME machinery +\n"
    "// per-draw GPU timestamps of patch_draw_timing.py).\n"
    "void RequestTraceFrame();\n"
    "bool ConsumeTraceRequest();\n"
    "}  // namespace rex::ht_diag\n")
print("include/rex/ht_diag_hooks.h: written")

patch("src/filesystem/virtual_file_system.cpp", [
    ("#include <rex/filesystem/vfs.h>\n",
     "#include <rex/filesystem/vfs.h>\n#include <rex/ht_diag_hooks.h>\n#include <atomic>\n"),
    ("namespace rex::filesystem {\n",
     "namespace rex::ht_diag {\n// " + MARK + "\n"
     "static std::atomic<FileOpenObserver> g_file_open_observer{nullptr};\n"
     "static std::atomic<SwapObserver> g_swap_observer{nullptr};\n"
     "void SetFileOpenObserver(FileOpenObserver o) { g_file_open_observer.store(o, std::memory_order_release); }\n"
     "FileOpenObserver GetFileOpenObserver() { return g_file_open_observer.load(std::memory_order_acquire); }\n"
     "void SetSwapObserver(SwapObserver o) { g_swap_observer.store(o, std::memory_order_release); }\n"
     "SwapObserver GetSwapObserver() { return g_swap_observer.load(std::memory_order_acquire); }\n"
     "static std::atomic<bool> g_trace_request{false};\n"
     "void RequestTraceFrame() { g_trace_request.store(true, std::memory_order_release); }\n"
     "bool ConsumeTraceRequest() {\n"
     "  return g_trace_request.load(std::memory_order_relaxed) &&\n"
     "         g_trace_request.exchange(false, std::memory_order_acq_rel);\n"
     "}\n"
     "}  // namespace rex::ht_diag\n\nnamespace rex::filesystem {\n"),
    ("  // TODO(gibbed): should 'is_directory' remain as a bool or should it be\n",
     "  if (auto ht_observer = rex::ht_diag::GetFileOpenObserver()) {\n    ht_observer(path);\n  }\n"
     "  // TODO(gibbed): should 'is_directory' remain as a bool or should it be\n"),
])

patch("src/graphics/vulkan/command_processor.cpp", [
    ("#include <rex/graphics/vulkan/command_processor.h>\n",
     "#include <rex/graphics/vulkan/command_processor.h>\n#include <rex/ht_diag_hooks.h>  // " + MARK + "\n"),
    # Frame traces on request (needs patch_draw_timing.py + patch_barrier_trace.py first):
    # the timestamp query pool always exists, and a request starts a trace like HT_TRACE_FRAME.
    ("  if (std::getenv(\"HT_TRACE_FRAME\")) {\n    pool_info.queryCount = kHtTq;\n",
     "  {  // ht_diag hooks: per-draw timing pool always available\n    pool_info.queryCount = kHtTq;\n"),
    ("    if (ht_trace_frame && ht_swaps == ht_trace_frame) {\n",
     "    if ((ht_trace_frame && ht_swaps == ht_trace_frame) ||\n"
     "        (!ht_trace_active && !ht_tq_done_submission_ && rex::ht_diag::ConsumeTraceRequest())) {\n"),
    ("void VulkanCommandProcessor::PerfOnSwap() {\n  if (!perf_enabled_) {\n",
     "void VulkanCommandProcessor::PerfOnSwap() {\n"
     "  if (auto ht_observer = rex::ht_diag::GetSwapObserver()) {\n    ht_observer();\n  }\n"
     "  if (!perf_enabled_) {\n"),
    ("  ++perf_reports_;\n  const uint64_t pipelines_total = c.pipelines_created;\n",
     "  // [ht-pipe]: running total of pipelines created (shader compile stutter per track).\n"
     "  REXGPU_WARN(\"[ht-pipe] t={:.0f}s pipelines_created_total={}\", double(perf_reports_ + 1) * 2.0,\n"
     "              uint64_t(c.pipelines_created));\n"
     "  {\n"
     "    // [ht-vram]: driver-reported usage/budget per heap (VK_EXT_memory_budget).\n"
     "    const ui::vulkan::VulkanDevice* vd = GetVulkanDevice();\n"
     "    if (vd->extensions().ext_EXT_memory_budget &&\n"
     "        vd->vulkan_instance()->functions().vkGetPhysicalDeviceMemoryProperties2) {\n"
     "      VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {};\n"
     "      budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;\n"
     "      VkPhysicalDeviceMemoryProperties2 props = {};\n"
     "      props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;\n"
     "      props.pNext = &budget;\n"
     "      vd->vulkan_instance()->functions().vkGetPhysicalDeviceMemoryProperties2(vd->physical_device(), &props);\n"
     "      double dev_used = 0, dev_budget = 0, host_used = 0, host_budget = 0;\n"
     "      std::string heaps;\n"
     "      for (uint32_t h = 0; h < props.memoryProperties.memoryHeapCount; ++h) {\n"
     "        const bool local = props.memoryProperties.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;\n"
     "        const double used = double(budget.heapUsage[h]) / 1048576.0;\n"
     "        const double bud = double(budget.heapBudget[h]) / 1048576.0;\n"
     "        const double size = double(props.memoryProperties.memoryHeaps[h].size) / 1048576.0;\n"
     "        (local ? dev_used : host_used) += used;\n"
     "        (local ? dev_budget : host_budget) += bud;\n"
     "        heaps += fmt::format(\" h{}{}={:.0f}/{:.0f}/{:.0f}\", h, local ? \"L\" : \"H\", used, bud, size);\n"
     "      }\n"
     "      REXGPU_WARN(\"[ht-vram] t={:.0f}s device_used={:.0f}MB device_budget={:.0f}MB host_used={:.0f}MB \"\n"
     "                  \"host_budget={:.0f}MB heaps(used/budget/size MB):{}\",\n"
     "                  double(perf_reports_ + 1) * 2.0, dev_used, dev_budget, host_used, host_budget, heaps);\n"
     "    }\n"
     "  }\n"
     "  ++perf_reports_;\n  const uint64_t pipelines_total = c.pipelines_created;\n"),
])
