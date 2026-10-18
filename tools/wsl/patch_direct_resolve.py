"""Direct resolves for the ReXGlue Vulkan backend (Hydro Thunder).

Measured (Nativa, scale 2): resolves cost 1.0 ms per frame, 0.69 ms of it the
render target dump (render target image -> EDRAM buffer, every sample, about
250 MB per frame) that the resolve copy shader then reads back. When the
resolved EDRAM span belongs to a single render target of a supported format
(8_8_8_8, 32_FLOAT, D24S8; 1x or 4x MSAA) and the copy shader is one of the
32bpp ones, the SDK's own resolve copy shader, with only its EDRAM reads
replaced (sdk-extra/ht_direct_resolve, tools/shaders/make_direct_resolve.py),
reads the render target image directly and the dump is skipped.
Cvars: ht_direct_resolve (default true); ht_direct_resolve_verify (test only:
also runs the original dump + copy and compares the destination word by word,
log [ht-dres-verify]); ht_direct_resolve_verify_break (test only: wrong render
target base, the positive control for the verify mode).

usage: python3 patch_direct_resolve.py /root/ht/sdk <dir with ht_direct_*.cs.glsl>
"""
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
glsl_dir = Path(sys.argv[2])
SHADERS = [("fast_32bpp_1x2xmsaa", (0,)), ("fast_32bpp_4xmsaa", (1,)), ("full_32bpp", (0, 1))]
KINDS = ["8888", "f32", "u32", "d24s8"]


def patch(path, old, new, count=1):
    s = path.read_text()
    assert s.count(old) == count, (path, s.count(old), old[:100])
    path.write_text(s.replace(old, new))


# 1. SPIR-V headers; table of (shader index, scaled, kind, ms) -> header.
spirv_dir = root / "src/graphics/shaders/vulkan_spirv"
names = {}
for si, (base, ms_list) in enumerate(SHADERS):
    for scaled in (0, 1):
        for kind, kn in enumerate(KINDS):
            for ms in ms_list:
                stem = f"ht_direct_{base}{'_scaled' if scaled else ''}_{kn}{'_ms' if ms else ''}"
                var = stem + "_cs"
                subprocess.run(["glslangValidator", "-V", "-S", "comp", "--target-env", "vulkan1.0",
                                "--vn", var, "-o", str(spirv_dir / f"{var}.h"),
                                str(glsl_dir / f"{stem}.cs.glsl")], check=True, stdout=subprocess.DEVNULL)
                names[(si, scaled, kind, ms)] = var
subprocess.run(["glslangValidator", "-V", "-S", "comp", "--target-env", "vulkan1.0", "--vn",
                "ht_direct_resolve_compare_cs", "-o", str(spirv_dir / "ht_direct_resolve_compare_cs.h"),
                str(glsl_dir / "ht_direct_resolve_compare.cs.glsl")], check=True, stdout=subprocess.DEVNULL)


def index(si, scaled, kind, ms):
    return ((si * 2 + scaled) * 4 + kind) * 2 + ms


# 2. Header.
vk_h = root / "include/rex/graphics/vulkan/render_target_cache.h"
if "ht_dres_pipelines_" not in vk_h.read_text():
    patch(vk_h, "\n private:\n  // ht_clear_transfer_cutout.\n", """
 private:
  // ht_direct_resolve.
  struct HtDres {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet source_set = VK_NULL_HANDLE;
    uint32_t rt_base = 0;
    uint32_t rt_pitch = 0;
  };
  void HtDirectResolveInit();
  void HtDirectResolveShutdown();
  bool HtDirectResolvePrepare(const draw_util::ResolveInfo& resolve_info,
                              draw_util::ResolveCopyShaderIndex copy_shader, bool scaled,
                              HtDres& out);
  void HtVerifyBarrier();
  VkPipelineLayout ht_dres_layout_color_ = VK_NULL_HANDLE;
  VkPipelineLayout ht_dres_layout_depth_ = VK_NULL_HANDLE;
  std::array<VkPipeline, 48> ht_dres_pipelines_{};
  VkPipelineLayout ht_dres_compare_layout_ = VK_NULL_HANDLE;
  VkPipeline ht_dres_compare_pipeline_ = VK_NULL_HANDLE;
  VkBuffer ht_dres_verify_buffers_[3] = {};
  VkDeviceMemory ht_dres_verify_memory_[3] = {};
  volatile uint32_t* ht_dres_verify_counters_ = nullptr;
  uint64_t ht_dres_stats_[4] = {};  // direct, verified, verified words, fallback
  // ht_clear_transfer_cutout.
""")

# 3. Implementation.
vk = root / "src/graphics/vulkan/render_target_cache.cpp"
if "HtDirectResolveInit" not in vk.read_text():
    s = vk.read_text()
    includes = "".join(f'#include "../shaders/vulkan_spirv/{v}.h"\n' for v in sorted(set(names.values())))
    includes += '#include "../shaders/vulkan_spirv/ht_direct_resolve_compare_cs.h"\n'
    # Shaders live in namespace shaders like the other generated headers.
    marker = "}  // namespace shaders"
    assert s.count(marker) >= 1
    s = s.replace(marker, includes + marker, 1)
    s = s.replace('REXCVAR_DEFINE_INT32(ht_clear_cutout_slots, 31, "GPU",',
                  'REXCVAR_DEFINE_BOOL(ht_direct_resolve, true, "GPU",\n'
                  '                    "Resolve straight from the render target image (no EDRAM dump) when possible")\n'
                  '    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);\n'
                  'REXCVAR_DEFINE_BOOL(ht_direct_resolve_verify, false, "GPU",\n'
                  '                    "Test only: also run the original resolve and compare every written word")\n'
                  '    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);\n'
                  'REXCVAR_DEFINE_BOOL(ht_direct_resolve_verify_break, false, "GPU",\n'
                  '                    "Test only: wrong render target base (positive control for the verify mode)");\n'
                  'REXCVAR_DEFINE_INT32(ht_clear_cutout_slots, 31, "GPU",', 1)
    vk.write_text(s)

    patch(vk, """  create_direct_resolve_pipeline_layout(descriptor_set_layout_sampled_image_x2_,
                                        &direct_resolve_pipeline_layout_depth_);
""", """  create_direct_resolve_pipeline_layout(descriptor_set_layout_sampled_image_x2_,
                                        &direct_resolve_pipeline_layout_depth_);
  HtDirectResolveInit();
""")
    patch(vk, "  direct_resolve_pipelines_.clear();\n",
          "  direct_resolve_pipelines_.clear();\n  HtDirectResolveShutdown();\n")

    # Resolve: decide, skip the dump unless verifying.
    patch(vk, """      bool direct_resolved = false;
      if (GetPath() == Path::kHostRenderTargets) {
        if (REXCVAR_GET(direct_host_resolve)) {""",
          """      bool direct_resolved = false;
      HtDres ht_dres;
      const bool ht_use_dres = GetPath() == Path::kHostRenderTargets &&
                               REXCVAR_GET(ht_direct_resolve) &&
                               HtDirectResolvePrepare(resolve_info, copy_shader,
                                                      draw_resolution_scaled, ht_dres);
      const bool ht_verify = ht_use_dres && ht_dres_compare_pipeline_ != VK_NULL_HANDLE;
      if (GetPath() == Path::kHostRenderTargets && (!ht_use_dres || ht_verify)) {
        if (REXCVAR_GET(direct_host_resolve)) {""")

    old_dispatch = """          UseEdramBuffer(EdramBufferUsage::kComputeRead);
          command_processor_.BindExternalComputePipeline(
              resolve_copy_pipelines_[size_t(copy_shader)]);"""
    new_dispatch = """          draw_util::ResolveCopyShaderConstants ht_constants = copy_shader_constants;
          if (!draw_resolution_scaled) {
            ht_constants.dest_base -= uint32_t(write_descriptor_set_dest_buffer_info.offset);
          }
          const VkBuffer ht_dest_buffer = write_descriptor_set_dest_buffer_info.buffer;
          const VkDeviceSize ht_dest_offset = write_descriptor_set_dest_buffer_info.offset;
          const VkDeviceSize ht_dest_size = write_descriptor_set_dest_buffer_info.range;
          const bool ht_verify_now = ht_verify && ht_dest_size <= (VkDeviceSize(64) << 20);
          if (ht_verify_now) {
            // Snapshot the old destination contents.
            HtVerifyBarrier();
            VkBufferCopy ht_region = {ht_dest_offset, 0, ht_dest_size};
            command_buffer.CmdVkCopyBuffer(ht_dest_buffer, ht_dres_verify_buffers_[0], 1,
                                           &ht_region);
            HtVerifyBarrier();
          }
          if (!ht_use_dres || ht_verify_now) {
          UseEdramBuffer(EdramBufferUsage::kComputeRead);
          command_processor_.BindExternalComputePipeline(
              resolve_copy_pipelines_[size_t(copy_shader)]);"""
    patch(vk, old_dispatch, new_dispatch)
    old_tail = """          command_processor_.SubmitBarriers(true);
          command_buffer.CmdVkDispatch(copy_group_count_x, copy_group_count_y, 1);

          // Invalidate textures and mark the range as scaled if needed."""
    new_tail = """          command_processor_.SubmitBarriers(true);
          command_buffer.CmdVkDispatch(copy_group_count_x, copy_group_count_y, 1);
          }
          if (ht_use_dres) {
            if (ht_verify_now) {
              // Reference = original result; then restore the old contents.
              HtVerifyBarrier();
              VkBufferCopy ht_to_ref = {ht_dest_offset, 0, ht_dest_size};
              command_buffer.CmdVkCopyBuffer(ht_dest_buffer, ht_dres_verify_buffers_[1], 1,
                                             &ht_to_ref);
              HtVerifyBarrier();
              VkBufferCopy ht_restore = {0, ht_dest_offset, ht_dest_size};
              command_buffer.CmdVkCopyBuffer(ht_dres_verify_buffers_[0], ht_dest_buffer, 1,
                                             &ht_restore);
              HtVerifyBarrier();
            } else if (ht_verify) {
              ++ht_dres_stats_[3];
            }
            command_processor_.BindExternalComputePipeline(ht_dres.pipeline);
            const VkDescriptorSet ht_sets[] = {ht_dres.source_set, descriptor_set_dest};
            command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, ht_dres.layout,
                                                   0, 2, ht_sets, 0, nullptr);
            uint32_t ht_push[11] = {};
            std::memcpy(ht_push, &ht_constants,
                        draw_resolution_scaled ? sizeof(ht_constants.dest_relative)
                                               : sizeof(ht_constants));
            ht_push[8] = ht_dres.rt_base + (REXCVAR_GET(ht_direct_resolve_verify_break) ? 1 : 0);
            ht_push[9] = ht_dres.rt_pitch;
            ht_push[10] = draw_resolution_scale_x() | (draw_resolution_scale_y() << 8);
            command_buffer.CmdVkPushConstants(ht_dres.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                              sizeof(ht_push), ht_push);
            command_processor_.SubmitBarriers(true);
            command_buffer.CmdVkDispatch(copy_group_count_x, copy_group_count_y, 1);
            ++ht_dres_stats_[0];
            if (ht_verify_now) {
              HtVerifyBarrier();
              VkDescriptorSet ht_cmp_sets[3];
              VkDescriptorBufferInfo ht_cmp_infos[3] = {
                  {ht_dest_buffer, ht_dest_offset, ht_dest_size},
                  {ht_dres_verify_buffers_[1], 0, ht_dest_size},
                  {ht_dres_verify_buffers_[2], 0, 16}};
              VkWriteDescriptorSet ht_cmp_writes[3] = {};
              bool ht_cmp_ok = true;
              for (uint32_t i = 0; i < 3; ++i) {
                ht_cmp_sets[i] = command_processor_.AllocateSingleTransientDescriptor(
                    VulkanCommandProcessor::SingleTransientDescriptorLayout::kStorageBufferCompute);
                ht_cmp_ok &= ht_cmp_sets[i] != VK_NULL_HANDLE;
                ht_cmp_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                ht_cmp_writes[i].dstSet = ht_cmp_sets[i];
                ht_cmp_writes[i].descriptorCount = 1;
                ht_cmp_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                ht_cmp_writes[i].pBufferInfo = &ht_cmp_infos[i];
              }
              if (ht_cmp_ok) {
                dfn.vkUpdateDescriptorSets(device, 3, ht_cmp_writes, 0, nullptr);
                command_processor_.BindExternalComputePipeline(ht_dres_compare_pipeline_);
                command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                                                       ht_dres_compare_layout_, 0, 3, ht_cmp_sets,
                                                       0, nullptr);
                const uint32_t ht_words = uint32_t(ht_dest_size / 4);
                command_buffer.CmdVkPushConstants(ht_dres_compare_layout_,
                                                  VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ht_words),
                                                  &ht_words);
                command_processor_.SubmitBarriers(true);
                command_buffer.CmdVkDispatch((ht_words + 63) / 64, 1, 1);
                HtVerifyBarrier();
                ++ht_dres_stats_[1];
                ht_dres_stats_[2] += ht_words;
              }
            }
            static auto ht_t0 = std::chrono::steady_clock::now();
            const auto ht_now = std::chrono::steady_clock::now();
            if (ht_now - ht_t0 >= std::chrono::seconds(10)) {
              ht_t0 = ht_now;
              if (ht_dres_verify_counters_) {
                REXGPU_WARN("[ht-dres-verify] direct={} verified={} words={} groups={} "
                            "skipped_large={} mismatches={}",
                            ht_dres_stats_[0], ht_dres_stats_[1], ht_dres_stats_[2],
                            ht_dres_verify_counters_[1], ht_dres_stats_[3],
                            ht_dres_verify_counters_[0]);
              } else if (HtGpuDiag()) {
                REXGPU_WARN("[ht-dres] direct={}", ht_dres_stats_[0]);
              }
            }
          }

          // Invalidate textures and mark the range as scaled if needed."""
    patch(vk, old_tail, new_tail)

    # Helpers.
    s = vk.read_text()
    marker = "bool VulkanRenderTargetCache::HtClearSlots("
    assert s.count(marker) == 1
    entries = []
    for (si, scaled, kind, ms), var in sorted(names.items()):
        entries.append(f"      {{{index(si, scaled, kind, ms)}, shaders::{var}, sizeof(shaders::{var})}},")
    table = "\n".join(entries)
    s = s.replace(marker, r'''void VulkanRenderTargetCache::HtDirectResolveInit() {
  if (!REXCVAR_GET(ht_direct_resolve)) {
    return;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device = command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  const VkDescriptorSetLayout storage_buffer_layout =
      command_processor_.GetSingleTransientDescriptorLayout(
          VulkanCommandProcessor::SingleTransientDescriptorLayout::kStorageBufferCompute);
  auto make_layout = [&](VkDescriptorSetLayout source_layout, VkPipelineLayout& out) {
    const VkDescriptorSetLayout layouts[] = {source_layout, storage_buffer_layout};
    VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 11};
    VkPipelineLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 2;
    info.pSetLayouts = layouts;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &range;
    if (dfn.vkCreatePipelineLayout(device, &info, nullptr, &out)) {
      out = VK_NULL_HANDLE;
    }
  };
  make_layout(descriptor_set_layout_sampled_image_, ht_dres_layout_color_);
  make_layout(descriptor_set_layout_sampled_image_x2_, ht_dres_layout_depth_);
  if (ht_dres_layout_color_ == VK_NULL_HANDLE || ht_dres_layout_depth_ == VK_NULL_HANDLE) {
    HtDirectResolveShutdown();
    return;
  }
  struct Code {
    uint32_t index;
    const uint32_t* code;
    size_t size;
  };
  static const Code codes[] = {
''' + table + r'''
  };
  uint32_t created = 0;
  for (const Code& c : codes) {
    // Kind 3 (index bit 1..2 == 3) is depth.
    const bool is_depth = ((c.index >> 1) & 3) == 3;
    ht_dres_pipelines_[c.index] = ui::vulkan::util::CreateComputePipeline(
        vulkan_device, is_depth ? ht_dres_layout_depth_ : ht_dres_layout_color_, c.code, c.size);
    created += ht_dres_pipelines_[c.index] != VK_NULL_HANDLE;
  }
  REXGPU_INFO("[ht-dres] direct resolves enabled ({} pipelines)", created);
  if (REXCVAR_GET(ht_direct_resolve_verify)) {
    const VkDescriptorSetLayout cmp_layouts[] = {storage_buffer_layout, storage_buffer_layout,
                                                 storage_buffer_layout};
    VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)};
    VkPipelineLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 3;
    info.pSetLayouts = cmp_layouts;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &range;
    bool ok = !dfn.vkCreatePipelineLayout(device, &info, nullptr, &ht_dres_compare_layout_);
    const VkDeviceSize sizes[3] = {VkDeviceSize(64) << 20, VkDeviceSize(64) << 20, 256};
    const ui::vulkan::util::MemoryPurpose purposes[3] = {
        ui::vulkan::util::MemoryPurpose::kDeviceLocal, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
        ui::vulkan::util::MemoryPurpose::kUpload};
    for (uint32_t i = 0; ok && i < 3; ++i) {
      ok = ui::vulkan::util::CreateDedicatedAllocationBuffer(
          vulkan_device, sizes[i],
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          purposes[i], ht_dres_verify_buffers_[i], ht_dres_verify_memory_[i]);
    }
    void* mapped = nullptr;
    if (ok && !dfn.vkMapMemory(device, ht_dres_verify_memory_[2], 0, VK_WHOLE_SIZE, 0, &mapped)) {
      ht_dres_verify_counters_ = static_cast<volatile uint32_t*>(mapped);
      for (uint32_t i = 0; i < 4; ++i) ht_dres_verify_counters_[i] = 0;
      ht_dres_compare_pipeline_ = ui::vulkan::util::CreateComputePipeline(
          vulkan_device, ht_dres_compare_layout_, shaders::ht_direct_resolve_compare_cs,
          sizeof(shaders::ht_direct_resolve_compare_cs));
    }
    REXGPU_WARN("[ht-dres-verify] proof mode {}",
                ht_dres_compare_pipeline_ != VK_NULL_HANDLE ? "on" : "FAILED");
  }
}

void VulkanRenderTargetCache::HtDirectResolveShutdown() {
  const ui::vulkan::VulkanDevice* const vulkan_device = command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  if (ht_dres_verify_counters_) {
    REXGPU_WARN("[ht-dres-verify] final direct={} verified={} words={} mismatches={}",
                ht_dres_stats_[0], ht_dres_stats_[1], ht_dres_stats_[2],
                ht_dres_verify_counters_[0]);
  }
  for (VkPipeline& pipeline : ht_dres_pipelines_) {
    if (pipeline != VK_NULL_HANDLE) dfn.vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }
  if (ht_dres_compare_pipeline_ != VK_NULL_HANDLE)
    dfn.vkDestroyPipeline(device, ht_dres_compare_pipeline_, nullptr);
  ht_dres_compare_pipeline_ = VK_NULL_HANDLE;
  for (VkPipelineLayout* layout :
       {&ht_dres_layout_color_, &ht_dres_layout_depth_, &ht_dres_compare_layout_}) {
    if (*layout != VK_NULL_HANDLE) dfn.vkDestroyPipelineLayout(device, *layout, nullptr);
    *layout = VK_NULL_HANDLE;
  }
  ht_dres_verify_counters_ = nullptr;
  for (uint32_t i = 0; i < 3; ++i) {
    if (ht_dres_verify_buffers_[i] != VK_NULL_HANDLE)
      dfn.vkDestroyBuffer(device, ht_dres_verify_buffers_[i], nullptr);
    if (ht_dres_verify_memory_[i] != VK_NULL_HANDLE)
      dfn.vkFreeMemory(device, ht_dres_verify_memory_[i], nullptr);
    ht_dres_verify_buffers_[i] = VK_NULL_HANDLE;
    ht_dres_verify_memory_[i] = VK_NULL_HANDLE;
  }
}

void VulkanRenderTargetCache::HtVerifyBarrier() {
  // Test mode only: a full memory barrier between the copies and dispatches.
  VkMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  command_processor_.SubmitBarriers(true);
  command_processor_.deferred_command_buffer().CmdVkPipelineBarrier(
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0,
      nullptr, 0, nullptr);
}

bool VulkanRenderTargetCache::HtDirectResolvePrepare(const draw_util::ResolveInfo& resolve_info,
                                                     draw_util::ResolveCopyShaderIndex copy_shader,
                                                     bool scaled, HtDres& out) {
  if (ht_dres_layout_color_ == VK_NULL_HANDLE) {
    return false;
  }
  uint32_t shader_index;
  switch (copy_shader) {
    case draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA:
      shader_index = 0;
      break;
    case draw_util::ResolveCopyShaderIndex::kFast32bpp4xMSAA:
      shader_index = 1;
      break;
    case draw_util::ResolveCopyShaderIndex::kFull32bpp:
      shader_index = 2;
      break;
    default:
      return false;
  }
  uint32_t dump_base, dump_row_length_used, dump_rows, dump_pitch;
  resolve_info.GetCopyEdramTileSpan(dump_base, dump_row_length_used, dump_rows, dump_pitch);
  GetResolveCopyRectanglesToDump(dump_base, dump_row_length_used, dump_rows, dump_pitch,
                                 dump_rectangles_);
  // One render target owning the whole span.
  if (dump_rectangles_.size() != 1) {
    return false;
  }
  const ResolveCopyDumpRectangle& rectangle = dump_rectangles_[0];
  if (!rectangle.render_target || rectangle.row_first != 0 || rectangle.rows != dump_rows ||
      rectangle.row_first_start != 0 ||
      rectangle.row_last_end != (dump_rows == 1 ? dump_row_length_used : dump_row_length_used)) {
    return false;
  }
  auto& vulkan_rt = *static_cast<VulkanRenderTarget*>(rectangle.render_target);
  const RenderTargetKey key = vulkan_rt.key();
  if (key.Is64bpp()) {
    return false;
  }
  uint32_t ms;
  if (key.msaa_samples == xenos::MsaaSamples::k1X) {
    ms = 0;
  } else if (key.msaa_samples == xenos::MsaaSamples::k4X) {
    ms = 1;
  } else {
    return false;
  }
  if ((shader_index == 0 && ms) || (shader_index == 1 && !ms)) {
    return false;
  }
  uint32_t kind;
  if (key.is_depth) {
    if (key.GetDepthFormat() != xenos::DepthRenderTargetFormat::kD24S8) {
      return false;
    }
    kind = 3;
  } else {
    bool is_uint = false;
    GetColorOwnershipTransferVulkanFormat(key.GetColorFormat(), key.msaa_samples, &is_uint);
    switch (key.GetColorFormat()) {
      case xenos::ColorRenderTargetFormat::k_8_8_8_8:
        if (is_uint) return false;
        kind = 0;
        break;
      case xenos::ColorRenderTargetFormat::k_32_FLOAT:
        kind = is_uint ? 2 : 1;
        break;
      default:
        return false;
    }
  }
  const VkPipeline pipeline =
      ht_dres_pipelines_[((shader_index * 2 + (scaled ? 1 : 0)) * 4 + kind) * 2 + ms];
  if (pipeline == VK_NULL_HANDLE) {
    return false;
  }
  // Same access as the dump.
  command_processor_.PushImageMemoryBarrier(
      vulkan_rt.image(),
      ui::vulkan::util::InitializeSubresourceRange(
          key.is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                       : VK_IMAGE_ASPECT_COLOR_BIT),
      vulkan_rt.current_stage_mask(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      vulkan_rt.current_access_mask(), VK_ACCESS_SHADER_READ_BIT, vulkan_rt.current_layout(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  vulkan_rt.SetUsage(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  out.pipeline = pipeline;
  out.layout = key.is_depth ? ht_dres_layout_depth_ : ht_dres_layout_color_;
  out.source_set = vulkan_rt.GetDescriptorSetTransferSource();
  out.rt_base = key.base_tiles;
  out.rt_pitch = key.GetPitchTiles();
  return true;
}

''' + marker, 1)
    vk.write_text(s)
print("patched")
