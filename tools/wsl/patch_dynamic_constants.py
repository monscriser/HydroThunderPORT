"""Experimental CPU optimization: dynamic UBO offsets + last descriptor reuse.
Same buffer, effective offset and range as the original, verified per draw.
Apply on clean (non-measurement) candidate SDK. Defaults OFF until validated.
"""
from pathlib import Path
import sys
r=Path(sys.argv[1]);h=r/'include/rex/graphics/vulkan/command_processor.h';p=r/'src/graphics/vulkan/command_processor.cpp'
t=h.read_text();s=p.read_text()
def rep(a,b):
    global s
    assert s.count(a)==1,(s.count(a),a[:100])
    s=s.replace(a,b)
anchor='  VkDescriptorSetLayout descriptor_set_layout_constants_ = VK_NULL_HANDLE;'
assert t.count(anchor)==1
t=t.replace(anchor,anchor+'''
  // Fixed at device initialization to match the constants descriptor layout.
  bool ht_dynamic_constants_enabled_ = false;
  VkDescriptorSet ht_dynamic_constants_set_ = VK_NULL_HANDLE;
  uint64_t ht_dynamic_constants_frame_ = UINT64_MAX;
  std::array<VkDescriptorBufferInfo, SpirvShaderTranslator::kConstantBufferCount> ht_dynamic_constants_infos_{};
  uint64_t ht_dynamic_constants_calls_ = 0, ht_dynamic_constants_requests_ = 0, ht_dynamic_constants_hits_ = 0;
  uint64_t ht_dynamic_constants_verified_ = 0, ht_dynamic_constants_mismatches_ = 0;
''')
rep('REXCVAR_DEFINE_BOOL(ht_zpd_batch_verify, false,','''REXCVAR_DEFINE_BOOL(ht_dynamic_constants, false, "GPU/Vulkan",
                    "Experimental CPU: reuse constant descriptors with exact dynamic offsets");
REXCVAR_DEFINE_BOOL(ht_dynamic_constants_verify, false, "GPU/Vulkan",
                    "TEST ONLY: compare buffer, effective offset and range against original bindings");
REXCVAR_DEFINE_BOOL(ht_dynamic_constants_verify_break, false, "GPU/Vulkan",
                    "TEST ONLY: perturb reference offset without changing rendering");
REXCVAR_DEFINE_BOOL(ht_zpd_batch_verify, false,''')
rep('''  // Guest draw constants.
  VkDescriptorSetLayoutBinding''','''  // Guest draw constants. Vulkan guarantees at least 8 dynamic UBOs per layout.
  static_assert(SpirvShaderTranslator::kConstantBufferCount <= 8);
  ht_dynamic_constants_enabled_ = REXCVAR_GET(ht_dynamic_constants);
  REXGPU_WARN("[ht-dynamic-constants] enabled={}", ht_dynamic_constants_enabled_);
  VkDescriptorSetLayoutBinding''')
rep('''    constants_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;''','''    constants_binding.descriptorType = ht_dynamic_constants_enabled_
        ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;''')
rep('''  constants_transient_descriptors_free_.clear();''','''  ht_dynamic_constants_set_ = VK_NULL_HANDLE;
  ht_dynamic_constants_frame_ = UINT64_MAX;
  constants_transient_descriptors_free_.clear();''')
start=s.index('  // Constant buffers.\n',s.index('bool VulkanCommandProcessor::UpdateBindings('))
end=s.index('  // Vertex shader textures and samplers.\n',start)
old=s[start:end]
new='''  // Constant buffers.
  std::array<VkDescriptorBufferInfo, SpirvShaderTranslator::kConstantBufferCount> ht_constant_infos;
  bool ht_publish_constants = false;
  VkDescriptorSet ht_publish_constants_set = VK_NULL_HANDLE;
  if (!(current_graphics_descriptor_set_values_up_to_date_ &
        (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetConstants))) {
    bool reuse_constants = false;
    if (ht_dynamic_constants_enabled_) {
      ++ht_dynamic_constants_requests_;
      if (ht_dynamic_constants_frame_ != frame_current_) {
        ht_dynamic_constants_set_ = VK_NULL_HANDLE;
        ht_dynamic_constants_frame_ = frame_current_;
      }
      reuse_constants = ht_dynamic_constants_set_ != VK_NULL_HANDLE;
      for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
        ht_constant_infos[i] = current_constant_buffer_infos_[i];
        // Keep high bits in the descriptor and low 32 bits in dynamic offsets.
        // This also handles buffers with offsets larger than UINT32_MAX.
        ht_constant_infos[i].offset &= ~VkDeviceSize(UINT32_MAX);
        const auto& cached = ht_dynamic_constants_infos_[i];
        const auto& wanted = ht_constant_infos[i];
        reuse_constants &= cached.buffer == wanted.buffer && cached.offset == wanted.offset && cached.range == wanted.range;
      }
      ht_dynamic_constants_hits_ += reuse_constants;
    }
    VkDescriptorSet constants_descriptor_set;
    if (reuse_constants) {
      constants_descriptor_set = ht_dynamic_constants_set_;
    } else {
      if (!constants_transient_descriptors_free_.empty()) {
        constants_descriptor_set = constants_transient_descriptors_free_.back();
        constants_transient_descriptors_free_.pop_back();
      } else {
        VkDescriptorPoolSize constants_descriptor_count;
        constants_descriptor_count.type = ht_dynamic_constants_enabled_
            ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        constants_descriptor_count.descriptorCount = SpirvShaderTranslator::kConstantBufferCount;
        constants_descriptor_set = transient_descriptor_allocator_uniform_buffer_.Allocate(
            descriptor_set_layout_constants_, &constants_descriptor_count, 1);
        if (constants_descriptor_set == VK_NULL_HANDLE) return false;
      }
      constants_transient_descriptors_used_.emplace_back(frame_current_, constants_descriptor_set);
      for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
        VkWriteDescriptorSet& write_constants = write_descriptor_sets[write_descriptor_set_count++];
        write_constants.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_constants.pNext = nullptr;
        write_constants.dstSet = constants_descriptor_set;
        write_constants.dstBinding = i;
        write_constants.dstArrayElement = 0;
        write_constants.descriptorCount = 1;
        write_constants.descriptorType = ht_dynamic_constants_enabled_
            ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write_constants.pImageInfo = nullptr;
        write_constants.pBufferInfo = ht_dynamic_constants_enabled_
            ? &ht_constant_infos[i] : &current_constant_buffer_infos_[i];
        write_constants.pTexelBufferView = nullptr;
      }
      ht_publish_constants = ht_dynamic_constants_enabled_;
      ht_publish_constants_set = constants_descriptor_set;
    }
    write_descriptor_set_bits |= UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetConstants;
    current_graphics_descriptor_sets_[SpirvShaderTranslator::kDescriptorSetConstants] = constants_descriptor_set;
  }
'''
s=s[:start]+new+s[end:]
rep('''  // Only make valid if all descriptor sets have been allocated and written
''','''  // Publish only AFTER vkUpdateDescriptorSets. A texture allocation failure
  // above must not expose a constants set that has never been written.
  if (ht_publish_constants) {
    // Record the infos actually passed to Vulkan, not a parallel prediction.
    for (uint32_t w = 0; w < write_descriptor_set_count; ++w) {
      const auto& emitted = write_descriptor_sets[w];
      if (emitted.dstSet == ht_publish_constants_set && emitted.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
        assert_true(emitted.descriptorCount == 1 && emitted.dstBinding < SpirvShaderTranslator::kConstantBufferCount);
        ht_dynamic_constants_infos_[emitted.dstBinding] = *emitted.pBufferInfo;
      }
    }
    ht_dynamic_constants_set_ = ht_publish_constants_set;
  }
  if (ht_dynamic_constants_enabled_) {
    ++ht_dynamic_constants_calls_;
    if ((ht_dynamic_constants_calls_ & ((UINT64_C(1) << 18) - 1)) == 0) {
      REXGPU_WARN("[ht-dynamic-constants] calls={} requests={} hits={} compared={} mismatches={}",
          ht_dynamic_constants_calls_, ht_dynamic_constants_requests_, ht_dynamic_constants_hits_,
          ht_dynamic_constants_verified_, ht_dynamic_constants_mismatches_);
    }
  }
  // Only make valid if all descriptor sets have been allocated and written
''')
rep('''    deferred_command_buffer_.CmdVkBindDescriptorSets(
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        current_guest_graphics_pipeline_layout_->GetPipelineLayout(), descriptor_set_index,
        descriptor_set_mask_tzcnt - descriptor_set_index,
        current_graphics_descriptor_sets_ + descriptor_set_index, 0, nullptr);''','''    const bool bind_dynamic_constants = ht_dynamic_constants_enabled_ &&
        descriptor_set_index <= SpirvShaderTranslator::kDescriptorSetConstants &&
        descriptor_set_mask_tzcnt > SpirvShaderTranslator::kDescriptorSetConstants;
    uint32_t dynamic_offsets[SpirvShaderTranslator::kConstantBufferCount];
    if (bind_dynamic_constants) {
      for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
        dynamic_offsets[i] = uint32_t(current_constant_buffer_infos_[i].offset);
      }
      if (REXCVAR_GET(ht_dynamic_constants_verify)) {
        for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
          const auto& original = current_constant_buffer_infos_[i];
          const auto& actual = ht_dynamic_constants_infos_[i];
          const VkDeviceSize expected_offset = original.offset ^ (REXCVAR_GET(ht_dynamic_constants_verify_break) ? 1 : 0);
          const VkDeviceSize effective_offset = actual.offset + dynamic_offsets[i];
          ++ht_dynamic_constants_verified_;
          ht_dynamic_constants_mismatches_ += actual.buffer != original.buffer || effective_offset != expected_offset || actual.range != original.range ||
              current_graphics_descriptor_sets_[SpirvShaderTranslator::kDescriptorSetConstants] != ht_dynamic_constants_set_;
        }
      }
    }
    deferred_command_buffer_.CmdVkBindDescriptorSets(
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        current_guest_graphics_pipeline_layout_->GetPipelineLayout(), descriptor_set_index,
        descriptor_set_mask_tzcnt - descriptor_set_index,
        current_graphics_descriptor_sets_ + descriptor_set_index,
        bind_dynamic_constants ? SpirvShaderTranslator::kConstantBufferCount : 0,
        bind_dynamic_constants ? dynamic_offsets : nullptr);''')
h.write_text(t);p.write_text(s)
print('Dynamic constant descriptor experiment applied (default OFF)')
