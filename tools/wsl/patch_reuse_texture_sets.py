"""CPU: reuse the texture/sampler descriptor set of the previous draw (ht_reuse_texture_sets).

Measured (Nativa, uncapped): the GPU command processor thread is the bottleneck
(busy 6.3 ms/frame, never waiting for the guest); "bindings" is its biggest part
(1.4 ms/frame for ~2500 draws). For every draw, the SDK allocates and writes new
vertex and pixel texture descriptor sets ("TODO: Reuse texture and sampler
bindings if not changed"). Now, per stage, if the descriptor set layout and every
VkDescriptorImageInfo (image views, layouts, samplers) are identical to those of
the last set written for that stage in the same frame, that set is reused instead
of allocating and writing a new one (the GPU sees exactly the same descriptors; the
set stays alive until the frame completes, like every transient set).
Cvar ht_reuse_texture_sets (default true). With ht_perf_log, [ht-bind] reports
reused / written sets every 10 s.

usage: python3 patch_reuse_texture_sets.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new, count=1):
    s = path.read_text()
    assert s.count(old) == count, (path, s.count(old), old[:120])
    path.write_text(s.replace(old, new))


h = root / "include/rex/graphics/vulkan/command_processor.h"
patch(h, "  VkQueryPool perf_query_pool_ = VK_NULL_HANDLE;\n",
      "  VkQueryPool perf_query_pool_ = VK_NULL_HANDLE;\n"
      "  // ht_reuse_texture_sets: last texture descriptor set written per stage (0 vertex, 1 pixel).\n"
      "  struct HtTextureSetCache {\n"
      "    uint64_t frame = UINT64_MAX;\n"
      "    VkDescriptorSetLayout layout = VK_NULL_HANDLE;\n"
      "    VkDescriptorSet set = VK_NULL_HANDLE;\n"
      "    uint32_t texture_count = 0;\n"
      "    uint32_t sampler_count = 0;\n"
      "    std::vector<VkDescriptorImageInfo> infos;\n"
      "  };\n"
      "  HtTextureSetCache ht_texture_set_cache_[2];\n"
      "  uint64_t ht_texture_sets_reused_ = 0, ht_texture_sets_written_ = 0;\n")

cp = root / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
anchor = 'REXCVAR_DEFINE_BOOL(ht_msaa_clear_remap, true, "GPU",'
assert s.count(anchor) == 1
s = s.replace(anchor, 'REXCVAR_DEFINE_BOOL(ht_reuse_texture_sets, true, "GPU",\n'
              '                    "Reuse the previous draw texture descriptor set when identical");\n'
              + anchor, 1)
cp.write_text(s)

patch(cp, """  // Write the new descriptor sets.

  // Consecutive bindings updated via a single VkWriteDescriptorSet must have""",
      """  // ht_reuse_texture_sets: same layout and same descriptors as the last set written for
  // this stage in this frame -> reuse that set.
  if (REXCVAR_GET(ht_reuse_texture_sets)) {
    struct HtStage {
      bool* write;
      uint32_t texture_count, sampler_count;
      size_t texture_offset, sampler_offset;
      VkDescriptorSetLayout layout;
      uint32_t set_index;
    };
    HtStage ht_stages[2] = {
        {&write_vertex_textures, texture_count_vertex, sampler_count_vertex,
         vertex_texture_image_info_offset, vertex_sampler_image_info_offset,
         current_guest_graphics_pipeline_layout_->descriptor_set_layout_textures_vertex_ref(),
         uint32_t(SpirvShaderTranslator::kDescriptorSetTexturesVertex)},
        {&write_pixel_textures, texture_count_pixel, sampler_count_pixel,
         pixel_texture_image_info_offset, pixel_sampler_image_info_offset,
         current_guest_graphics_pipeline_layout_->descriptor_set_layout_textures_pixel_ref(),
         uint32_t(SpirvShaderTranslator::kDescriptorSetTexturesPixel)}};
    for (uint32_t ht_i = 0; ht_i < 2; ++ht_i) {
      HtStage& st = ht_stages[ht_i];
      if (!*st.write) {
        continue;
      }
      HtTextureSetCache& c = ht_texture_set_cache_[ht_i];
      const VkDescriptorImageInfo* tex = descriptor_write_image_info_.data() + st.texture_offset;
      const VkDescriptorImageInfo* smp = descriptor_write_image_info_.data() + st.sampler_offset;
      bool same = c.frame == frame_current_ && c.layout == st.layout && c.set != VK_NULL_HANDLE &&
                  c.texture_count == st.texture_count && c.sampler_count == st.sampler_count &&
                  c.infos.size() == size_t(st.texture_count) + st.sampler_count;
      for (uint32_t k = 0; same && k < st.texture_count; ++k) {
        same = c.infos[k].imageView == tex[k].imageView &&
               c.infos[k].imageLayout == tex[k].imageLayout;
      }
      for (uint32_t k = 0; same && k < st.sampler_count; ++k) {
        same = c.infos[st.texture_count + k].sampler == smp[k].sampler;
      }
      if (same) {
        *st.write = false;
        current_graphics_descriptor_sets_[st.set_index] = c.set;
        current_graphics_descriptor_set_values_up_to_date_ |= UINT32_C(1) << st.set_index;
        ++ht_texture_sets_reused_;
      } else {
        c.frame = frame_current_;
        c.layout = st.layout;
        c.set = VK_NULL_HANDLE;  // Filled once written below.
        c.texture_count = st.texture_count;
        c.sampler_count = st.sampler_count;
        c.infos.assign(tex, tex + st.texture_count);
        c.infos.insert(c.infos.end(), smp, smp + st.sampler_count);
        ++ht_texture_sets_written_;
      }
    }
  }

  // Write the new descriptor sets.

  // Consecutive bindings updated via a single VkWriteDescriptorSet must have""")
patch(cp, """    write_descriptor_set_bits |= UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex;
    current_graphics_descriptor_sets_[SpirvShaderTranslator::kDescriptorSetTexturesVertex] =
        write_textures[0].dstSet;
  }""", """    write_descriptor_set_bits |= UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex;
    current_graphics_descriptor_sets_[SpirvShaderTranslator::kDescriptorSetTexturesVertex] =
        write_textures[0].dstSet;
    if (ht_texture_set_cache_[0].frame == frame_current_) {
      ht_texture_set_cache_[0].set = write_textures[0].dstSet;
    }
  }""")
patch(cp, """    write_descriptor_set_bits |= UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel;
    current_graphics_descriptor_sets_[SpirvShaderTranslator::kDescriptorSetTexturesPixel] =
        write_textures[0].dstSet;
  }""", """    write_descriptor_set_bits |= UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel;
    current_graphics_descriptor_sets_[SpirvShaderTranslator::kDescriptorSetTexturesPixel] =
        write_textures[0].dstSet;
    if (ht_texture_set_cache_[1].frame == frame_current_) {
      ht_texture_set_cache_[1].set = write_textures[0].dstSet;
    }
  }""")
# Report with the [ht-draw] line.
s = cp.read_text()
marker = '"[ht-draw] t='
assert s.count(marker) == 1, s.count(marker)
idx = s.index(marker)
stmt_start = s.rindex("REXGPU_WARN(", 0, idx)
s = s[:stmt_start] + """if (REXCVAR_GET(ht_reuse_texture_sets)) {
      REXGPU_WARN("[ht-bind] texture sets reused={} written={}", ht_texture_sets_reused_,
                  ht_texture_sets_written_);
    }
    """ + s[stmt_start:]
cp.write_text(s)
print("patched reuse texture sets")
