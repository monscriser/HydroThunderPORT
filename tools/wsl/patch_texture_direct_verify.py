"""Proof mode for ht_texture_direct (cvar ht_texture_direct_verify, default off).

Each direct texture load also runs the original SDK load shader into the
scratch buffer, and a compute shader compares every texel of the texture with
it. Every 10 s the log gets:
  [ht-direct-verify] loads=N texels=M groups=G mismatches=K
K must be 0. Only for tests (doubles the texture load cost).

usage: python3 patch_texture_direct_verify.py /root/ht/sdk <dir with ht_texture_direct_verify.cs.glsl>
"""
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
glsl_dir = Path(sys.argv[2])


def patch(path, old, new, count=1):
    s = path.read_text()
    assert s.count(old) == count, (path, s.count(old), old[:100])
    path.write_text(s.replace(old, new))


spirv_dir = root / "src/graphics/shaders/vulkan_spirv"
subprocess.run(["glslangValidator", "-V", "-S", "comp", "--target-env", "vulkan1.0", "--vn",
                "ht_texture_direct_verify_cs", "-o", str(spirv_dir / "ht_texture_direct_verify_cs.h"),
                str(glsl_dir / "ht_texture_direct_verify.cs.glsl")], check=True, stdout=subprocess.DEVNULL)

tch = root / "include/rex/graphics/vulkan/texture_cache.h"
if "ht_verify_pipeline_" not in tch.read_text():
    patch(tch, "  std::array<VkPipeline, 6> ht_direct_pipelines_{};\n",
          """  std::array<VkPipeline, 6> ht_direct_pipelines_{};
  VkPipelineLayout ht_verify_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline ht_verify_pipeline_ = VK_NULL_HANDLE;
  VkBuffer ht_verify_buffer_ = VK_NULL_HANDLE;
  VmaAllocation ht_verify_allocation_ = nullptr;
  volatile uint32_t* ht_verify_mapped_ = nullptr;
  uint64_t ht_verify_loads_ = 0;
  uint64_t ht_verify_texels_ = 0;
""")

tc = root / "src/graphics/vulkan/texture_cache.cpp"
if "ht_verify_pipeline_" not in tc.read_text():
    patch(tc, '#include "../shaders/vulkan_spirv/ht_texture_direct_32bpb_cs.h"\n',
          '#include "../shaders/vulkan_spirv/ht_texture_direct_32bpb_cs.h"\n'
          '#include "../shaders/vulkan_spirv/ht_texture_direct_verify_cs.h"\n')
    patch(tc, 'REXCVAR_DEFINE_BOOL(ht_texture_blit, true, "GPU",',
          'REXCVAR_DEFINE_BOOL(ht_texture_direct_verify, false, "GPU",\n'
          '                    "Test only: compare every direct texture load with the original load shader")\n'
          '    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);\n'
          'REXCVAR_DEFINE_BOOL(ht_texture_blit, true, "GPU",')
    # Second (original) load into the scratch buffer, right after the direct dispatch.
    patch(tc, """      command_processor_.SubmitBarriers(true);
      command_buffer.CmdVkDispatch(group_count_x, group_count_y, load_constants.size_blocks[2]);
""", """      command_processor_.SubmitBarriers(true);
      command_buffer.CmdVkDispatch(group_count_x, group_count_y, load_constants.size_blocks[2]);
      if (ht_direct_pipeline != VK_NULL_HANDLE && ht_verify_pipeline_ != VK_NULL_HANDLE) {
        command_processor_.BindExternalComputePipeline(pipeline);
        command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, load_pipeline_layout_,
                                               kLoadDescriptorSetIndexDestination, 1,
                                               &descriptor_set_dest, 0, nullptr);
        command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, load_pipeline_layout_,
                                               kLoadDescriptorSetIndexSource, 1,
                                               &descriptor_set_source, 0, nullptr);
        command_buffer.CmdVkPushConstants(load_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                          sizeof(load_constants), &load_constants);
        command_buffer.CmdVkDispatch(group_count_x, group_count_y, load_constants.size_blocks[2]);
      }
""")
    patch(tc, """  if (ht_direct_pipeline != VK_NULL_HANDLE) {
    return true;
  }
""", """  if (ht_direct_pipeline != VK_NULL_HANDLE) {
    if (ht_verify_pipeline_ != VK_NULL_HANDLE) {
      command_processor_.PushBufferMemoryBarrier(
          scratch_buffer, 0, VK_WHOLE_SIZE,
          scratch_buffer_acquisition.SetStageMask(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          scratch_buffer_acquisition.SetAccessMask(VK_ACCESS_SHADER_WRITE_BIT),
          VK_ACCESS_SHADER_READ_BIT);
      command_processor_.PushImageMemoryBarrier(
          vulkan_texture.image(), ui::vulkan::util::InitializeSubresourceRange(),
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, false);
      VkDescriptorSet ht_counter_set = command_processor_.AllocateSingleTransientDescriptor(
          VulkanCommandProcessor::SingleTransientDescriptorLayout::kStorageBufferCompute);
      if (ht_counter_set) {
        VkDescriptorBufferInfo ht_counter_info = {ht_verify_buffer_, 0, 16};
        VkWriteDescriptorSet ht_write = {};
        ht_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ht_write.dstSet = ht_counter_set;
        ht_write.descriptorCount = 1;
        ht_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ht_write.pBufferInfo = &ht_counter_info;
        dfn.vkUpdateDescriptorSets(device, 1, &ht_write, 0, nullptr);
        command_processor_.SubmitBarriers(true);
        command_processor_.BindExternalComputePipeline(ht_verify_pipeline_);
        const VkDescriptorSet ht_sets[] = {descriptor_set_dest, vulkan_texture.ht_direct_set_,
                                           ht_counter_set};
        command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                                               ht_verify_pipeline_layout_, 0, 3, ht_sets, 0,
                                               nullptr);
        const uint32_t ht_w = width * texture_resolution_scale_x;
        const uint32_t ht_h = height * texture_resolution_scale_y;
        const uint32_t ht_constants[4] = {uint32_t(host_layout_base.offset_bytes / 4),
                                          host_layout_base.x_pitch_blocks, ht_w, ht_h};
        command_buffer.CmdVkPushConstants(ht_verify_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                                          0, sizeof(ht_constants), ht_constants);
        command_buffer.CmdVkDispatch((ht_w + 7) / 8, (ht_h + 7) / 8, 1);
        ++ht_verify_loads_;
        ht_verify_texels_ += uint64_t(ht_w) * ht_h;
        static auto ht_verify_t0 = std::chrono::steady_clock::now();
        const auto ht_verify_now = std::chrono::steady_clock::now();
        if (ht_verify_now - ht_verify_t0 >= std::chrono::seconds(10)) {
          ht_verify_t0 = ht_verify_now;
          REXGPU_WARN("[ht-direct-verify] loads={} texels={} groups={} mismatches={}",
                      ht_verify_loads_, ht_verify_texels_, ht_verify_mapped_[1],
                      ht_verify_mapped_[0]);
        }
      }
    }
    return true;
  }
""")
    # Initialization / shutdown.
    patch(tc, """  REXGPU_INFO("[ht-direct] direct texture loads enabled");
}""", """  REXGPU_INFO("[ht-direct] direct texture loads enabled");
  if (REXCVAR_GET(ht_texture_direct_verify)) {
    const VkDescriptorSetLayout storage_buffer_layout =
        command_processor_.GetSingleTransientDescriptorLayout(
            VulkanCommandProcessor::SingleTransientDescriptorLayout::kStorageBufferCompute);
    const VkDescriptorSetLayout verify_layouts[] = {storage_buffer_layout, ht_direct_set_layout_,
                                                    storage_buffer_layout};
    VkPushConstantRange verify_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 4};
    VkPipelineLayoutCreateInfo verify_layout_info = {};
    verify_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    verify_layout_info.setLayoutCount = 3;
    verify_layout_info.pSetLayouts = verify_layouts;
    verify_layout_info.pushConstantRangeCount = 1;
    verify_layout_info.pPushConstantRanges = &verify_range;
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = 16;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VmaAllocationCreateInfo buffer_allocation_info = {};
    buffer_allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    buffer_allocation_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    buffer_allocation_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VmaAllocationInfo allocation_result;
    if (!dfn.vkCreatePipelineLayout(device, &verify_layout_info, nullptr,
                                    &ht_verify_pipeline_layout_) &&
        !vmaCreateBuffer(vma_allocator_, &buffer_info, &buffer_allocation_info, &ht_verify_buffer_,
                         &ht_verify_allocation_, &allocation_result)) {
      ht_verify_mapped_ = static_cast<volatile uint32_t*>(allocation_result.pMappedData);
      for (uint32_t i = 0; i < 4; ++i) ht_verify_mapped_[i] = 0;
      ht_verify_pipeline_ = ui::vulkan::util::CreateComputePipeline(
          vulkan_device, ht_verify_pipeline_layout_, shaders::ht_texture_direct_verify_cs,
          sizeof(shaders::ht_texture_direct_verify_cs));
    }
    REXGPU_WARN("[ht-direct-verify] proof mode {}",
                ht_verify_pipeline_ != VK_NULL_HANDLE ? "on" : "FAILED");
  }
}""")
    patch(tc, """  for (VkPipeline& pipeline : ht_direct_pipelines_) {
    if (pipeline != VK_NULL_HANDLE) dfn.vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }
""", """  for (VkPipeline& pipeline : ht_direct_pipelines_) {
    if (pipeline != VK_NULL_HANDLE) dfn.vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }
  if (ht_verify_pipeline_ != VK_NULL_HANDLE) {
    REXGPU_WARN("[ht-direct-verify] final loads={} texels={} groups={} mismatches={}",
                ht_verify_loads_, ht_verify_texels_, ht_verify_mapped_[1], ht_verify_mapped_[0]);
    dfn.vkDestroyPipeline(device, ht_verify_pipeline_, nullptr);
  }
  ht_verify_pipeline_ = VK_NULL_HANDLE;
  if (ht_verify_pipeline_layout_ != VK_NULL_HANDLE)
    dfn.vkDestroyPipelineLayout(device, ht_verify_pipeline_layout_, nullptr);
  ht_verify_pipeline_layout_ = VK_NULL_HANDLE;
  if (ht_verify_buffer_ != VK_NULL_HANDLE)
    vmaDestroyBuffer(vma_allocator_, ht_verify_buffer_, ht_verify_allocation_);
  ht_verify_buffer_ = VK_NULL_HANDLE;
  ht_verify_mapped_ = nullptr;
""")
print("patched")
