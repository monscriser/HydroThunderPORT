"""Resolve straight into the texture (ht_resolve_to_texture) for the ReXGlue Vulkan backend.

Measured (Nativa, rev 8.5): every one of the 18 resolves per frame is followed
by a reload (texload, 0.75 ms per frame) of the texture that samples the
resolved memory: the load shader reads the words the resolve has just written
and decodes them into the texture. When that texture is already known (it was
loaded before through the ht_texture_direct path: 2D, tiled, 1 level, 32 bit
copy / depth unorm / depth float load shader, same scaled-resolve state), contains
the resolve destination base with the same pitch, and the resolve rectangle
either covers the whole texture or the texture is up to date before the resolve
(the game resolves some 1024-wide targets in two 512-wide halves), the direct
resolve shader (variant from
tools/shaders/make_resolve_texture.py) also writes the decoded texels into the
texture, and the texture is marked up to date right after the resolve marks the
memory as written, so the reload is skipped.

Cvars: ht_resolve_to_texture (default true); ht_resolve_texture_verify (test
only, needs ht_texture_direct_verify=true: after each such resolve, also runs
the original load shader into a buffer and compares every texel with the
texture, mismatches counted in [ht-direct-verify]); ht_resolve_texture_verify_break
(test only: texels shifted by 1 pixel, the positive control).

usage: python3 patch_resolve_texture.py /root/ht/sdk <dir with ht_rtt_*.cs.glsl>
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
    assert s.count(old) == count, (path, s.count(old), old[:120])
    path.write_text(s.replace(old, new))


def index(si, scaled, kind, ms):
    return ((si * 2 + scaled) * 4 + kind) * 2 + ms


# 1. SPIR-V headers.
spirv_dir = root / "src/graphics/shaders/vulkan_spirv"
names = {}
for si, (base, ms_list) in enumerate(SHADERS):
    for scaled in (0, 1):
        for kind, kn in enumerate(KINDS):
            for ms in ms_list:
                stem = f"ht_rtt_{base}{'_scaled' if scaled else ''}_{kn}{'_ms' if ms else ''}"
                var = stem + "_cs"
                subprocess.run(["glslangValidator", "-V", "-S", "comp", "--target-env", "vulkan1.0",
                                "--vn", var, "-o", str(spirv_dir / f"{var}.h"),
                                str(glsl_dir / f"{stem}.cs.glsl")], check=True, stdout=subprocess.DEVNULL)
                names[index(si, scaled, kind, ms)] = var

# 2. Generic texture cache: mark a texture up to date from a subclass.
gen_h = root / "include/rex/graphics/pipeline/texture/cache.h"
if "HtMarkTextureUpToDate" not in gen_h.read_text():
    patch(gen_h, "  bool LoadTextureData(Texture& texture);\n",
          "  bool LoadTextureData(Texture& texture);\n"
          "  // ht_resolve_to_texture: the texture data was written by a resolve.\n"
          "  void HtMarkTextureUpToDate(Texture& texture) {\n"
          "    texture.MakeUpToDateAndWatch(global_critical_region_.Acquire());\n"
          "  }\n")

# 3. Vulkan texture cache header.
tc_h = root / "include/rex/graphics/vulkan/texture_cache.h"
if "HtRttFind" not in tc_h.read_text():
    patch(tc_h, """  void UseScaledResolveBufferForWrite(uint64_t written_start_scaled,
                                      uint64_t written_length_scaled);
""", """  void UseScaledResolveBufferForWrite(uint64_t written_start_scaled,
                                      uint64_t written_length_scaled);

  // ht_resolve_to_texture: a known texture a resolve can write directly.
  struct HtRttTarget {
    void* texture = nullptr;
    VkDescriptorSet set = VK_NULL_HANDLE;
    // Texture endianness | load conversion (0 copy, 1 depth unorm, 2 depth float) << 2.
    uint32_t info = 0;
    // Scaled texel position of the destination base: x | y << 16.
    uint32_t offset = 0;
  };
  VkDescriptorSetLayout ht_direct_set_layout() const { return ht_direct_set_layout_; }
  bool ht_verify_available() const { return ht_verify_pipeline_ != VK_NULL_HANDLE; }
  // Finds the texture containing dest_base with this pitch that the resolve rectangle
  // (x0, y0, width, height relative to dest_base, unscaled) covers, or a part of
  // which it writes while the texture is up to date, and makes it writable by the
  // resolve compute shader.
  bool HtRttFind(uint32_t dest_base, uint32_t pitch_div_32, uint32_t x0, uint32_t y0,
                 uint32_t width, uint32_t height, bool scaled, HtRttTarget& out);
  // After the resolve marked the range as written: the texture is up to date.
  // verify: also run the original load shader and compare (proof mode).
  void HtRttFinish(const HtRttTarget& target, bool verify);
""")
    patch(tc_h, """    VkImageView ht_direct_view_ = VK_NULL_HANDLE;
    VkDescriptorSet ht_direct_set_ = VK_NULL_HANDLE;
""", """    VkImageView ht_direct_view_ = VK_NULL_HANDLE;
    VkDescriptorSet ht_direct_set_ = VK_NULL_HANDLE;
    // ht_resolve_to_texture: load conversion once loaded directly, 0xFF if not a candidate.
    uint8_t ht_rtt_conv_ = 0xFF;
""")
    patch(tc_h, "  uint64_t ht_verify_texels_ = 0;\n",
          "  uint64_t ht_verify_texels_ = 0;\n"
          "  std::vector<VulkanTexture*> ht_rtt_candidates_;\n"
          "  bool ht_rtt_verify_only_ = false;\n")

# 4. Vulkan texture cache implementation.
tc = root / "src/graphics/vulkan/texture_cache.cpp"
if "HtRttFind" not in tc.read_text():
    patch(tc, """  if (ht_direct_pipeline != VK_NULL_HANDLE) {
    vulkan_texture.MarkAsUsed();
""", """  if (ht_direct_pipeline != VK_NULL_HANDLE) {
    vulkan_texture.MarkAsUsed();
    if (vulkan_texture.ht_rtt_conv_ == 0xFF && texture_key.tiled && !is_3d &&
        texture_key.mip_max_level == 0) {
      vulkan_texture.ht_rtt_conv_ = uint8_t(load_shader == kLoadShaderIndex32bpb         ? 0
                                            : load_shader == kLoadShaderIndexDepthUnorm ? 1
                                                                                        : 2);
      ht_rtt_candidates_.push_back(&vulkan_texture);
    }
""")
    patch(tc, """      command_processor_.SubmitBarriers(true);
      command_buffer.CmdVkDispatch(group_count_x, group_count_y, load_constants.size_blocks[2]);
      if (ht_direct_pipeline != VK_NULL_HANDLE && ht_verify_pipeline_ != VK_NULL_HANDLE) {""",
          """      command_processor_.SubmitBarriers(true);
      if (!ht_rtt_verify_only_) {
        command_buffer.CmdVkDispatch(group_count_x, group_count_y, load_constants.size_blocks[2]);
      }
      if (ht_direct_pipeline != VK_NULL_HANDLE && ht_verify_pipeline_ != VK_NULL_HANDLE) {""")
    patch(tc, """VulkanTextureCache::VulkanTexture::~VulkanTexture() {
  const VulkanTextureCache& vulkan_texture_cache =
      static_cast<const VulkanTextureCache&>(texture_cache());
""", """VulkanTextureCache::VulkanTexture::~VulkanTexture() {
  const VulkanTextureCache& vulkan_texture_cache =
      static_cast<const VulkanTextureCache&>(texture_cache());
  if (ht_rtt_conv_ != 0xFF) {
    auto& candidates = const_cast<VulkanTextureCache&>(vulkan_texture_cache).ht_rtt_candidates_;
    candidates.erase(std::remove(candidates.begin(), candidates.end(), this), candidates.end());
  }
""")
    s = tc.read_text()
    marker = "void VulkanTextureCache::HtBlitInitialize() {"
    assert s.count(marker) == 1
    s = s.replace(marker, r'''bool VulkanTextureCache::HtRttFind(uint32_t dest_base, uint32_t pitch_div_32, uint32_t x0,
                                   uint32_t y0, uint32_t width, uint32_t height, bool scaled,
                                   HtRttTarget& out) {
  dest_base &= 0x1FFFFFFF;
  for (VulkanTexture* texture : ht_rtt_candidates_) {
    const TextureKey& key = texture->key();
    const uint32_t base = key.base_page << 12;
    if (dest_base < base || dest_base - base >= texture->GetGuestBaseSize() ||
        key.pitch != pitch_div_32 || !key.tiled ||
        key.dimension != xenos::DataDimension::k2DOrStacked || key.GetDepthOrArraySize() != 1 ||
        key.mip_max_level != 0 || bool(key.scaled_resolve) != scaled ||
        texture->ht_direct_set_ == VK_NULL_HANDLE) {
      continue;
    }
    // Position of the destination base inside the texture (32bpp, 2D tiled).
    uint32_t bx = 0, by = 0;
    bool found = dest_base == base;
    if (!found) {
      const uint32_t gx = UINT32_C(1) << xenos::GetTextureTiledXBaseGranularityLog2(false, 2);
      const uint32_t gy = UINT32_C(1) << xenos::GetTextureTiledYBaseGranularityLog2(false, 2);
      for (uint32_t y = 0; !found && y < key.GetHeight(); y += gy) {
        for (uint32_t x = 0; x < key.GetWidth(); x += gx) {
          if (base + uint32_t(texture_util::GetTiledOffset2D(int32_t(x), int32_t(y),
                                                             key.pitch << 5, 2)) == dest_base) {
            bx = x;
            by = y;
            found = true;
            break;
          }
        }
      }
    }
    if (!found) {
      continue;
    }
    const uint32_t rx = bx + x0, ry = by + y0;
    const bool covers =
        rx == 0 && ry == 0 && width >= key.GetWidth() && height >= key.GetHeight();
    if ((!covers && texture->outdated_mask() != 0) || rx >= key.GetWidth() ||
        ry >= key.GetHeight()) {
      continue;
    }
    if (scaled && !EnsureScaledResolveMemoryCommitted(dest_base, texture->GetGuestBaseSize(), 4)) {
      return false;
    }
    texture->MarkAsUsed();
    VulkanTexture::Usage old_usage = texture->SetUsage(VulkanTexture::Usage::kHtStorageWrite);
    VkPipelineStageFlags src_stage, dst_stage;
    VkAccessFlags src_access, dst_access;
    VkImageLayout old_layout, new_layout;
    GetTextureUsageMasks(old_usage, src_stage, src_access, old_layout);
    GetTextureUsageMasks(VulkanTexture::Usage::kHtStorageWrite, dst_stage, dst_access, new_layout);
    if (old_usage == VulkanTexture::Usage::kUndefined) {
      src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    command_processor_.PushImageMemoryBarrier(
        texture->image(), ui::vulkan::util::InitializeSubresourceRange(), src_stage, dst_stage,
        src_access, dst_access, old_layout, new_layout, VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED, false);
    out.texture = texture;
    out.set = texture->ht_direct_set_;
    out.info = uint32_t(key.endianness) | (uint32_t(texture->ht_rtt_conv_) << 2);
    const uint32_t sx = scaled ? draw_resolution_scale_x() : 1;
    const uint32_t sy = scaled ? draw_resolution_scale_y() : 1;
    out.offset = (bx * sx) | ((by * sy) << 16);
    return true;
  }
  return false;
}

void VulkanTextureCache::HtRttFinish(const HtRttTarget& target, bool verify) {
  VulkanTexture& texture = *static_cast<VulkanTexture*>(target.texture);
  HtMarkTextureUpToDate(texture);
  if (verify && ht_verify_pipeline_ != VK_NULL_HANDLE) {
    ht_rtt_verify_only_ = true;
    LoadTextureDataFromResidentMemoryImpl(texture, true, false);
    ht_rtt_verify_only_ = false;
  }
}

''' + marker, 1)
    tc.write_text(s)

# 5. Render target cache header.
rt_h = root / "include/rex/graphics/vulkan/render_target_cache.h"
if "ht_rtt_pipelines_" not in rt_h.read_text():
    patch(rt_h, "  uint64_t ht_dres_stats_[4] = {};  // direct, verified, verified words, fallback\n",
          """  uint64_t ht_dres_stats_[4] = {};  // direct, verified, verified words, fallback
  // ht_resolve_to_texture.
  void HtRttInit(VkDescriptorSetLayout texture_set_layout);
  bool ht_rtt_init_done_ = false;
  VkPipelineLayout ht_rtt_layout_color_ = VK_NULL_HANDLE;
  VkPipelineLayout ht_rtt_layout_depth_ = VK_NULL_HANDLE;
  std::array<VkPipeline, 48> ht_rtt_pipelines_{};
  uint64_t ht_rtt_stats_[3] = {};  // written, no texture, verified
""")

# 6. Render target cache implementation.
vk = root / "src/graphics/vulkan/render_target_cache.cpp"
if "HtRttInit" not in vk.read_text():
    s = vk.read_text()
    includes = "".join(f'#include "../shaders/vulkan_spirv/{v}.h"\n' for v in sorted(set(names.values())))
    marker = "}  // namespace shaders"
    s = s.replace(marker, includes + marker, 1)
    old_cvar = 'REXCVAR_DEFINE_BOOL(ht_direct_resolve_verify_break, false, "GPU",'
    assert s.count(old_cvar) == 1
    s = s.replace(old_cvar,
                  'REXCVAR_DEFINE_BOOL(ht_resolve_to_texture, true, "GPU",\n'
                  '                    "Direct resolves also write the texture that would be reloaded from them")\n'
                  '    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);\n'
                  'REXCVAR_DEFINE_BOOL(ht_resolve_texture_verify, false, "GPU",\n'
                  '                    "Test only (with ht_texture_direct_verify): compare every texel written by a "\n'
                  '                    "resolve with the original texture load")\n'
                  '    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);\n'
                  'REXCVAR_DEFINE_BOOL(ht_resolve_texture_verify_break, false, "GPU",\n'
                  '                    "Test only: texels shifted by 1 pixel (positive control for the verify mode)");\n'
                  + old_cvar, 1)
    vk.write_text(s)

    # Pick the texture before binding the direct resolve pipeline.
    patch(vk, """            command_processor_.BindExternalComputePipeline(ht_dres.pipeline);
            const VkDescriptorSet ht_sets[] = {ht_dres.source_set, descriptor_set_dest};
            command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, ht_dres.layout,
                                                   0, 2, ht_sets, 0, nullptr);
            uint32_t ht_push[11] = {};""", """            VkPipeline ht_pipeline = ht_dres.pipeline;
            VkPipelineLayout ht_layout = ht_dres.layout;
            VkDescriptorSet ht_sets[3] = {ht_dres.source_set, descriptor_set_dest, VK_NULL_HANDLE};
            uint32_t ht_set_count = 2;
            if (!ht_verify_now && REXCVAR_GET(ht_resolve_to_texture) &&
                !resolve_info.copy_dest_info.copy_dest_array) {
              if (!ht_rtt_init_done_) {
                HtRttInit(texture_cache.ht_direct_set_layout());
              }
              uint32_t ht_pi = 0;
              while (ht_pi < ht_dres_pipelines_.size() && ht_dres_pipelines_[ht_pi] != ht_pipeline) {
                ++ht_pi;
              }
              if (ht_pi < ht_rtt_pipelines_.size() && ht_rtt_pipelines_[ht_pi] != VK_NULL_HANDLE &&
                  texture_cache.HtRttFind(
                      resolve_info.copy_dest_base,
                      resolve_info.copy_dest_coordinate_info.pitch_aligned_div_32,
                      uint32_t(resolve_info.copy_dest_coordinate_info.offset_x_div_8) << 3,
                      uint32_t(resolve_info.copy_dest_coordinate_info.offset_y_div_8) << 3,
                      resolve_info.coordinate_info.width_div_8 << 3, resolve_info.height_div_8 << 3,
                      draw_resolution_scaled, ht_rtt)) {
                ht_rtt_used = true;
                ht_pipeline = ht_rtt_pipelines_[ht_pi];
                ht_layout = ht_layout == ht_dres_layout_depth_ ? ht_rtt_layout_depth_
                                                               : ht_rtt_layout_color_;
                ht_sets[2] = ht_rtt.set;
                ht_set_count = 3;
                ++ht_rtt_stats_[0];
              } else {
                ++ht_rtt_stats_[1];
              }
            }
            command_processor_.BindExternalComputePipeline(ht_pipeline);
            command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, ht_layout,
                                                   0, ht_set_count, ht_sets, 0, nullptr);
            uint32_t ht_push[13] = {};""")
    patch(vk, """            ht_push[10] = draw_resolution_scale_x() | (draw_resolution_scale_y() << 8);
            command_buffer.CmdVkPushConstants(ht_dres.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                              sizeof(ht_push), ht_push);""",
          """            ht_push[10] = draw_resolution_scale_x() | (draw_resolution_scale_y() << 8);
            ht_push[11] = ht_rtt.info |
                          (REXCVAR_GET(ht_resolve_texture_verify_break) ? UINT32_C(256) : 0);
            ht_push[12] = ht_rtt.offset;
            command_buffer.CmdVkPushConstants(
                ht_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                ht_set_count == 3 ? sizeof(ht_push) : sizeof(uint32_t) * 11, ht_push);""")
    patch(vk, """              if (ht_dres_verify_counters_) {
                REXGPU_WARN("[ht-dres-verify] direct={} verified={} words={} groups={} "
""", """              if (ht_rtt_init_done_ && (HtGpuDiag() || REXCVAR_GET(ht_resolve_texture_verify))) {
                REXGPU_WARN("[ht-rtt] to_texture={} no_texture={} verified={}", ht_rtt_stats_[0],
                            ht_rtt_stats_[1], ht_rtt_stats_[2]);
              }
              if (ht_dres_verify_counters_) {
                REXGPU_WARN("[ht-dres-verify] direct={} verified={} words={} groups={} "
""")
    # Declare the target before the dispatch block, finish after the range is marked.
    patch(vk, """      bool direct_resolved = false;
      HtDres ht_dres;
""", """      bool direct_resolved = false;
      HtDres ht_dres;
      VulkanTextureCache::HtRttTarget ht_rtt;
      bool ht_rtt_used = false;
""")
    patch(vk, """          texture_cache.MarkRangeAsResolved(resolve_info.copy_dest_extent_start,
                                            resolve_info.copy_dest_extent_length);
""", """          texture_cache.MarkRangeAsResolved(resolve_info.copy_dest_extent_start,
                                            resolve_info.copy_dest_extent_length);
          if (ht_rtt_used) {
            const bool ht_rtt_verify =
                REXCVAR_GET(ht_resolve_texture_verify) && texture_cache.ht_verify_available();
            texture_cache.HtRttFinish(ht_rtt, ht_rtt_verify);
            ht_rtt_stats_[2] += ht_rtt_verify ? 1 : 0;
          }
""")
    patch(vk, "  direct_resolve_pipelines_.clear();\n  HtDirectResolveShutdown();\n",
          """  direct_resolve_pipelines_.clear();
  HtDirectResolveShutdown();
  {
    const ui::vulkan::VulkanDevice* const ht_device = command_processor_.GetVulkanDevice();
    for (VkPipeline& pipeline : ht_rtt_pipelines_) {
      if (pipeline != VK_NULL_HANDLE)
        ht_device->functions().vkDestroyPipeline(ht_device->device(), pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    for (VkPipelineLayout* layout : {&ht_rtt_layout_color_, &ht_rtt_layout_depth_}) {
      if (*layout != VK_NULL_HANDLE)
        ht_device->functions().vkDestroyPipelineLayout(ht_device->device(), *layout, nullptr);
      *layout = VK_NULL_HANDLE;
    }
    ht_rtt_init_done_ = false;
  }
""")

    s = vk.read_text()
    marker = "void VulkanRenderTargetCache::HtVerifyBarrier() {"
    assert s.count(marker) == 1
    table = "\n".join(f"      {{{i}, shaders::{v}, sizeof(shaders::{v})}}," for i, v in sorted(names.items()))
    s = s.replace(marker, r'''void VulkanRenderTargetCache::HtRttInit(VkDescriptorSetLayout texture_set_layout) {
  ht_rtt_init_done_ = true;
  if (texture_set_layout == VK_NULL_HANDLE) {
    return;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device = command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  const VkDescriptorSetLayout storage_buffer_layout =
      command_processor_.GetSingleTransientDescriptorLayout(
          VulkanCommandProcessor::SingleTransientDescriptorLayout::kStorageBufferCompute);
  auto make_layout = [&](VkDescriptorSetLayout source_layout, VkPipelineLayout& out) {
    const VkDescriptorSetLayout layouts[] = {source_layout, storage_buffer_layout,
                                             texture_set_layout};
    VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 13};
    VkPipelineLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 3;
    info.pSetLayouts = layouts;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &range;
    if (dfn.vkCreatePipelineLayout(device, &info, nullptr, &out)) {
      out = VK_NULL_HANDLE;
    }
  };
  make_layout(descriptor_set_layout_sampled_image_, ht_rtt_layout_color_);
  make_layout(descriptor_set_layout_sampled_image_x2_, ht_rtt_layout_depth_);
  if (ht_rtt_layout_color_ == VK_NULL_HANDLE || ht_rtt_layout_depth_ == VK_NULL_HANDLE) {
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
    const bool is_depth = ((c.index >> 1) & 3) == 3;
    ht_rtt_pipelines_[c.index] = ui::vulkan::util::CreateComputePipeline(
        vulkan_device, is_depth ? ht_rtt_layout_depth_ : ht_rtt_layout_color_, c.code, c.size);
    created += ht_rtt_pipelines_[c.index] != VK_NULL_HANDLE;
  }
  REXGPU_INFO("[ht-rtt] resolve to texture enabled ({} pipelines)", created);
}

''' + marker, 1)
    vk.write_text(s)
print("patched")
