"""Texture upload compute blit for the ReXGlue Vulkan backend (Hydro Thunder).

Measured: at Nativa (draw scale 2) the buffer->image copy of reloaded
render-to-texture data costs 2.6 ms of 11 ms GPU per frame on an RTX 5060 Ti.
Single-level 2D uncompressed loads now go scratch buffer -> compute -> integer
staging image -> vkCmdCopyImage. Disable with the cvar ht_texture_blit=false.

usage: python3 patch_texture_blit.py /root/ht/sdk <dir with ht_texture_blit_*.cs.glsl>
"""
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
glsl_dir = Path(sys.argv[2])


def patch(path, old, new, count=1):
    s = path.read_text()
    assert old in s, (path, old[:80])
    path.write_text(s.replace(old, new, count))


# 1. SPIR-V headers.
spirv_dir = root / "src/graphics/shaders/vulkan_spirv"
for bpb in (32, 64, 128):
    name = f"ht_texture_blit_{bpb}bpb_cs"
    subprocess.run(["glslangValidator", "-V", "-S", "comp", "--target-env", "vulkan1.0", "--vn", name,
                    "-o", str(spirv_dir / f"{name}.h"), str(glsl_dir / f"ht_texture_blit_{bpb}bpb.cs.glsl")],
                   check=True, stdout=subprocess.DEVNULL)

# 2. vkCmdCopyImage in the device function table and the deferred command buffer.
inc = root / "include/rex/ui/vulkan/functions/device_1_0.inc"
if "XE_UI_VULKAN_FUNCTION(vkCmdCopyImage)" not in inc.read_text():
    patch(inc, "XE_UI_VULKAN_FUNCTION(vkCmdCopyBufferToImage)\n",
          "XE_UI_VULKAN_FUNCTION(vkCmdCopyBufferToImage)\nXE_UI_VULKAN_FUNCTION(vkCmdCopyImage)\n")

dch = root / "include/rex/graphics/vulkan/deferred_command_buffer.h"
if "kVkCopyImage" not in dch.read_text():
    patch(dch, "  void CmdVkCopyQueryPoolResults(",
          """  void CmdVkCopyImage(VkImage src_image, VkImageLayout src_image_layout, VkImage dst_image,
                      VkImageLayout dst_image_layout, const VkImageCopy& region) {
    auto& args = *reinterpret_cast<ArgsVkCopyImage*>(
        WriteCommand(Command::kVkCopyImage, sizeof(ArgsVkCopyImage)));
    args.src_image = src_image;
    args.src_image_layout = src_image_layout;
    args.dst_image = dst_image;
    args.dst_image_layout = dst_image_layout;
    args.region = region;
  }

  void CmdVkCopyQueryPoolResults(""")
    patch(dch, "    kVkCopyBufferToImage,\n", "    kVkCopyBufferToImage,\n    kVkCopyImage,\n")
    patch(dch, "  struct ArgsVkCopyBufferToImage {",
          """  struct ArgsVkCopyImage {
    VkImage src_image;
    VkImageLayout src_image_layout;
    VkImage dst_image;
    VkImageLayout dst_image_layout;
    VkImageCopy region;
  };

  struct ArgsVkCopyBufferToImage {""")

dcc = root / "src/graphics/vulkan/deferred_command_buffer.cpp"
if "Command::kVkCopyImage" not in dcc.read_text():
    patch(dcc, "      case Command::kVkCopyQueryPoolResults: {",
          """      case Command::kVkCopyImage: {
        auto& args = *reinterpret_cast<const ArgsVkCopyImage*>(stream);
        dfn.vkCmdCopyImage(command_buffer, args.src_image, args.src_image_layout, args.dst_image,
                           args.dst_image_layout, 1, &args.region);
      } break;

      case Command::kVkCopyQueryPoolResults: {""")

# 3. Texture cache members.
tch = root / "include/rex/graphics/vulkan/texture_cache.h"
if "ht_blit_pipeline_layout_" not in tch.read_text():
    patch(tch, "#include <array>\n", "#include <array>\n#include <map>\n")
    patch(tch, "  VkPipelineLayout load_pipeline_layout_ = VK_NULL_HANDLE;\n",
          """  VkPipelineLayout load_pipeline_layout_ = VK_NULL_HANDLE;
  // ht: compute blit for texture uploads (cvar ht_texture_blit).
  struct HtBlitStaging {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    bool initialized = false;
  };
  void HtBlitInitialize();
  void HtBlitShutdown();
  HtBlitStaging* HtBlitGetStaging(uint32_t index, uint32_t width, uint32_t height);
  VkDescriptorSetLayout ht_blit_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool ht_blit_pool_ = VK_NULL_HANDLE;
  VkPipelineLayout ht_blit_pipeline_layout_ = VK_NULL_HANDLE;
  std::array<VkPipeline, 3> ht_blit_pipelines_{};
  std::map<uint64_t, HtBlitStaging> ht_blit_staging_;
  uint64_t ht_blit_staging_bytes_ = 0;
""")

# 4. Texture cache implementation.
tc = root / "src/graphics/vulkan/texture_cache.cpp"
s = tc.read_text()
if "HtBlitInitialize" not in s:
    patch(tc, "}  // namespace shaders",
          '#include "../shaders/vulkan_spirv/ht_texture_blit_32bpb_cs.h"\n'
          '#include "../shaders/vulkan_spirv/ht_texture_blit_64bpb_cs.h"\n'
          '#include "../shaders/vulkan_spirv/ht_texture_blit_128bpb_cs.h"\n'
          "}  // namespace shaders")
    patch(tc, 'REXCVAR_DEFINE_BOOL(non_seamless_cube_map, false, "GPU", "Use non-seamless cube map sampling");',
          'REXCVAR_DEFINE_BOOL(non_seamless_cube_map, false, "GPU", "Use non-seamless cube map sampling");\n'
          'REXCVAR_DEFINE_BOOL(ht_texture_blit, true, "GPU",\n'
          '                    "Upload single-level 2D textures with a compute blit and an image copy instead "\n'
          '                    "of a buffer-to-image copy (much faster on NVIDIA)")\n'
          '    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);')
    # Shutdown before VMA goes away.
    patch(tc, "  // Textures memory is allocated using the Vulkan Memory Allocator, destroy all\n",
          "  HtBlitShutdown();\n\n  // Textures memory is allocated using the Vulkan Memory Allocator, destroy all\n")
    # Initialize after the load pipelines.
    patch(tc, "  // Use true null descriptors when supported; otherwise, keep compatibility\n",
          "  HtBlitInitialize();\n\n  // Use true null descriptors when supported; otherwise, keep compatibility\n")
    # Upload path.
    patch(tc, """  // Submit copying from the copy buffer to the host texture.
  command_processor_.PushBufferMemoryBarrier(
      scratch_buffer, 0, VK_WHOLE_SIZE,
      scratch_buffer_acquisition.SetStageMask(VK_PIPELINE_STAGE_TRANSFER_BIT),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      scratch_buffer_acquisition.SetAccessMask(VK_ACCESS_TRANSFER_READ_BIT),
      VK_ACCESS_TRANSFER_READ_BIT);
""", """  // ht: hot render-to-texture reloads are single-level 2D uncompressed images;
  // write them through a compute blit and an image copy (see ht_texture_blit).
  HtBlitStaging* ht_blit = nullptr;
  const uint32_t ht_blit_width = width * texture_resolution_scale_x;
  const uint32_t ht_blit_height = height * texture_resolution_scale_y;
  if (ht_blit_pipeline_layout_ != VK_NULL_HANDLE && level_first == 0 && level_last == 0 && !is_3d &&
      array_size == 1 && host_block_width == 1 && host_block_height == 1 &&
      block_width == 1 && block_height == 1) {
    const uint32_t bpb = load_shader_info.bytes_per_host_block;
    const uint32_t index = bpb == 4 ? 0 : bpb == 8 ? 1 : bpb == 16 ? 2 : UINT32_MAX;
    if (index != UINT32_MAX && ht_blit_pipelines_[index] != VK_NULL_HANDLE &&
        host_layout_base.offset_bytes % bpb == 0) {
      ht_blit = HtBlitGetStaging(index, ht_blit_width, ht_blit_height);
      if (ht_blit) {
        command_processor_.PushBufferMemoryBarrier(
            scratch_buffer, 0, VK_WHOLE_SIZE,
            scratch_buffer_acquisition.SetStageMask(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            scratch_buffer_acquisition.SetAccessMask(VK_ACCESS_SHADER_READ_BIT),
            VK_ACCESS_SHADER_READ_BIT);
        command_processor_.PushImageMemoryBarrier(
            ht_blit->image, ui::vulkan::util::InitializeSubresourceRange(),
            ht_blit->initialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            ht_blit->initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0, VK_ACCESS_SHADER_WRITE_BIT,
            ht_blit->initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, false);
        ht_blit->initialized = true;
        command_processor_.SubmitBarriers(true);
        command_processor_.BindExternalComputePipeline(ht_blit_pipelines_[index]);
        const VkDescriptorSet ht_blit_sets[] = {descriptor_set_dest, ht_blit->descriptor_set};
        command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                                               ht_blit_pipeline_layout_, 0, 2, ht_blit_sets, 0,
                                               nullptr);
        const uint32_t ht_blit_constants[4] = {
            uint32_t(host_layout_base.offset_bytes / bpb), host_layout_base.x_pitch_blocks,
            ht_blit_width, ht_blit_height};
        command_buffer.CmdVkPushConstants(ht_blit_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                          sizeof(ht_blit_constants), ht_blit_constants);
        command_buffer.CmdVkDispatch((ht_blit_width + 7) / 8, (ht_blit_height + 7) / 8, 1);
        command_processor_.PushImageMemoryBarrier(
            ht_blit->image, ui::vulkan::util::InitializeSubresourceRange(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, false);
      }
    }
  }

  // Submit copying from the copy buffer to the host texture.
  if (!ht_blit) {
  command_processor_.PushBufferMemoryBarrier(
      scratch_buffer, 0, VK_WHOLE_SIZE,
      scratch_buffer_acquisition.SetStageMask(VK_PIPELINE_STAGE_TRANSFER_BIT),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      scratch_buffer_acquisition.SetAccessMask(VK_ACCESS_TRANSFER_READ_BIT),
      VK_ACCESS_TRANSFER_READ_BIT);
  }
""")
    patch(tc, """  command_processor_.SubmitBarriers(true);
  VkBufferImageCopy* copy_regions = command_buffer.CmdCopyBufferToImageEmplace(""",
          """  command_processor_.SubmitBarriers(true);
  if (ht_blit) {
    VkImageCopy region = {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource = region.srcSubresource;
    region.extent.width = ht_blit_width;
    region.extent.height = ht_blit_height;
    region.extent.depth = 1;
    command_buffer.CmdVkCopyImage(ht_blit->image, VK_IMAGE_LAYOUT_GENERAL, vulkan_texture.image(),
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region);
  } else {
  VkBufferImageCopy* copy_regions = command_buffer.CmdCopyBufferToImageEmplace(""")
    patch(tc, """    copy_region.imageExtent.depth = std::max(depth >> level, UINT32_C(1));
  }
  command_processor_.PerfGpuEnd(VulkanCommandProcessor::PerfGpu::kTexCopy);""",
          """    copy_region.imageExtent.depth = std::max(depth >> level, UINT32_C(1));
  }
  }
  command_processor_.PerfGpuEnd(VulkanCommandProcessor::PerfGpu::kTexCopy);""")
    # Helpers, appended at the end of the namespace block of the file.
    s = tc.read_text()
    marker = "void VulkanTextureCache::UpdateTextureBindingsImpl("
    assert marker in s
    s = s.replace(marker, r'''void VulkanTextureCache::HtBlitInitialize() {
  if (!REXCVAR_GET(ht_texture_blit)) {
    return;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device = command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanInstance::Functions& ifn = vulkan_device->vulkan_instance()->functions();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkDescriptorSetLayoutBinding binding = {};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo set_layout_info = {};
  set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  set_layout_info.bindingCount = 1;
  set_layout_info.pBindings = &binding;
  if (dfn.vkCreateDescriptorSetLayout(device, &set_layout_info, nullptr, &ht_blit_set_layout_)) {
    HtBlitShutdown();
    return;
  }
  VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64};
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 64;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  if (dfn.vkCreateDescriptorPool(device, &pool_info, nullptr, &ht_blit_pool_)) {
    HtBlitShutdown();
    return;
  }
  const VkDescriptorSetLayout set_layouts[] = {
      command_processor_.GetSingleTransientDescriptorLayout(
          VulkanCommandProcessor::SingleTransientDescriptorLayout::kStorageBufferCompute),
      ht_blit_set_layout_};
  VkPushConstantRange push_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 4};
  VkPipelineLayoutCreateInfo layout_info = {};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.setLayoutCount = 2;
  layout_info.pSetLayouts = set_layouts;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  if (dfn.vkCreatePipelineLayout(device, &layout_info, nullptr, &ht_blit_pipeline_layout_)) {
    HtBlitShutdown();
    return;
  }
  static const VkFormat formats[] = {VK_FORMAT_R32_UINT, VK_FORMAT_R16G16B16A16_UINT,
                                     VK_FORMAT_R32G32B32A32_UINT};
  const std::pair<const uint32_t*, size_t> code[] = {
      {shaders::ht_texture_blit_32bpb_cs, sizeof(shaders::ht_texture_blit_32bpb_cs)},
      {shaders::ht_texture_blit_64bpb_cs, sizeof(shaders::ht_texture_blit_64bpb_cs)},
      {shaders::ht_texture_blit_128bpb_cs, sizeof(shaders::ht_texture_blit_128bpb_cs)}};
  uint32_t ready = 0;
  for (size_t i = 0; i < 3; ++i) {
    VkFormatProperties properties;
    ifn.vkGetPhysicalDeviceFormatProperties(vulkan_device->physical_device(), formats[i],
                                            &properties);
    const VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((properties.optimalTilingFeatures & needed) != needed) {
      continue;
    }
    ht_blit_pipelines_[i] = ui::vulkan::util::CreateComputePipeline(
        vulkan_device, ht_blit_pipeline_layout_, code[i].first, code[i].second);
    ready += ht_blit_pipelines_[i] != VK_NULL_HANDLE;
  }
  if (!ready) {
    HtBlitShutdown();
    return;
  }
  REXGPU_INFO("[ht-blit] texture upload compute blit enabled ({} of 3 sizes)", ready);
}

void VulkanTextureCache::HtBlitShutdown() {
  const ui::vulkan::VulkanDevice* const vulkan_device = command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  for (auto& [key, staging] : ht_blit_staging_) {
    if (staging.view != VK_NULL_HANDLE) dfn.vkDestroyImageView(device, staging.view, nullptr);
    if (staging.image != VK_NULL_HANDLE) vmaDestroyImage(vma_allocator_, staging.image, staging.allocation);
  }
  ht_blit_staging_.clear();
  ht_blit_staging_bytes_ = 0;
  for (VkPipeline& pipeline : ht_blit_pipelines_) {
    if (pipeline != VK_NULL_HANDLE) dfn.vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }
  if (ht_blit_pipeline_layout_ != VK_NULL_HANDLE)
    dfn.vkDestroyPipelineLayout(device, ht_blit_pipeline_layout_, nullptr);
  ht_blit_pipeline_layout_ = VK_NULL_HANDLE;
  if (ht_blit_pool_ != VK_NULL_HANDLE) dfn.vkDestroyDescriptorPool(device, ht_blit_pool_, nullptr);
  ht_blit_pool_ = VK_NULL_HANDLE;
  if (ht_blit_set_layout_ != VK_NULL_HANDLE)
    dfn.vkDestroyDescriptorSetLayout(device, ht_blit_set_layout_, nullptr);
  ht_blit_set_layout_ = VK_NULL_HANDLE;
}

VulkanTextureCache::HtBlitStaging* VulkanTextureCache::HtBlitGetStaging(uint32_t index,
                                                                          uint32_t width,
                                                                          uint32_t height) {
  // One staging image per size, kept for the session: the textures reloaded
  // every frame have a handful of fixed sizes. Capped at 256 MB.
  const uint64_t key = (uint64_t(index) << 48) | (uint64_t(width) << 24) | height;
  auto it = ht_blit_staging_.find(key);
  if (it != ht_blit_staging_.end()) {
    return it->second.image != VK_NULL_HANDLE ? &it->second : nullptr;
  }
  HtBlitStaging& staging = ht_blit_staging_[key];
  const uint64_t bytes = uint64_t(width) * height * (UINT64_C(4) << index);
  if (ht_blit_staging_bytes_ + bytes > (UINT64_C(256) << 20)) {
    return nullptr;
  }
  static const VkFormat formats[] = {VK_FORMAT_R32_UINT, VK_FORMAT_R16G16B16A16_UINT,
                                     VK_FORMAT_R32G32B32A32_UINT};
  const ui::vulkan::VulkanDevice* const vulkan_device = command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = formats[index];
  image_info.extent = {width, height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VmaAllocationCreateInfo allocation_info = {};
  allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  VkImage image;
  VmaAllocation allocation;
  if (vmaCreateImage(vma_allocator_, &image_info, &allocation_info, &image, &allocation, nullptr)) {
    return nullptr;
  }
  VkImageViewCreateInfo view_info = {};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = formats[index];
  view_info.subresourceRange = ui::vulkan::util::InitializeSubresourceRange();
  VkImageView view;
  if (dfn.vkCreateImageView(device, &view_info, nullptr, &view)) {
    vmaDestroyImage(vma_allocator_, image, allocation);
    return nullptr;
  }
  VkDescriptorSetAllocateInfo set_info = {};
  set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_info.descriptorPool = ht_blit_pool_;
  set_info.descriptorSetCount = 1;
  set_info.pSetLayouts = &ht_blit_set_layout_;
  VkDescriptorSet set;
  if (dfn.vkAllocateDescriptorSets(device, &set_info, &set)) {
    dfn.vkDestroyImageView(device, view, nullptr);
    vmaDestroyImage(vma_allocator_, image, allocation);
    return nullptr;
  }
  VkDescriptorImageInfo descriptor_image = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  write.pImageInfo = &descriptor_image;
  dfn.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  staging.image = image;
  staging.allocation = allocation;
  staging.view = view;
  staging.descriptor_set = set;
  ht_blit_staging_bytes_ += bytes;
  return &staging;
}

''' + marker, 1)
    tc.write_text(s)
print("patched")
