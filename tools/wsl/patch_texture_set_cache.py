#!/usr/bin/env python3
"""CPU: per-frame cache of texture/sampler descriptor sets (ht_texture_set_cache, default true).

ROG Ally run (11/11/2026, RADV, 20 W): the GPU command thread is the FPS limiter on 8 of 11
tracks (97-104% of a core) and ~20% of it is vkUpdateDescriptorSets called from
UpdateBindings. For EVERY draw the SDK allocates and writes new vertex and pixel texture
descriptor sets ("TODO(Triang3l): Reuse texture and sampler bindings if not changed").
Hydro draws each scene twice per frame (the two halves of the 360's predicated tiling),
so most texture combinations repeat within a frame.

Now, per stage (vertex / pixel), a set written in the current frame is kept in a small
hash table keyed by (descriptor set layout, every VkDescriptorImageInfo: image view,
layout, sampler). A later draw of the same frame with exactly the same inputs uses that
set instead of allocating and writing a new one. Exact by construction: the reused set
was written with byte-identical VkDescriptorImageInfos, and like every transient set it
stays alive until the frame's submission completes; the views/samplers it references
were used this frame, so they cannot be destroyed before that either. The table is
cleared when frame_current_ changes.

[ht-bind] (with ht_perf_log, every 2 s report): sets reused / written.
ht_texture_set_cache_verify: also allocates and writes the set as before and compares
the inputs of the cached entry with the fresh ones (counts mismatches; must be 0);
ht_texture_set_cache_verify_break perturbs one cached input (positive control).
usage: python3 patch_texture_set_cache.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new, count=1):
    s = path.read_text()
    assert s.count(old) == count, (path, s.count(old), old[:120])
    path.write_text(s.replace(old, new))


h = root / "include/rex/graphics/vulkan/command_processor.h"
if "ht_texture_set_cache_" in h.read_text():
    print("already patched")
    sys.exit(0)
patch(h, "  VkQueryPool perf_query_pool_ = VK_NULL_HANDLE;\n",
      "  VkQueryPool perf_query_pool_ = VK_NULL_HANDLE;\n"
      "  // ht_texture_set_cache: texture descriptor sets written this frame, per stage\n"
      "  // (0 vertex, 1 pixel), keyed by layout + image infos.\n"
      "  struct HtTextureSetEntry {\n"
      "    VkDescriptorSetLayout layout = VK_NULL_HANDLE;\n"
      "    VkDescriptorSet set = VK_NULL_HANDLE;\n"
      "    uint32_t texture_count = 0, sampler_count = 0;\n"
      "    std::vector<VkDescriptorImageInfo> infos;\n"
      "  };\n"
      "  std::unordered_multimap<uint64_t, HtTextureSetEntry> ht_texture_set_cache_[2];\n"
      "  uint64_t ht_texture_set_cache_frame_ = UINT64_MAX;\n"
      "  uint64_t ht_texture_set_pending_key_[2] = {};\n"
      "  bool ht_texture_set_pending_[2] = {};\n"
      "  uint64_t ht_texture_sets_reused_ = 0, ht_texture_sets_written_ = 0;\n"
      "  uint64_t ht_texture_sets_verified_ = 0, ht_texture_sets_mismatches_ = 0;\n")
hs = h.read_text()
if "#include <unordered_map>" not in hs:
    hs = hs.replace("#include <vector>\n", "#include <unordered_map>\n#include <vector>\n", 1)
    assert "#include <unordered_map>" in hs
    h.write_text(hs)

cp = root / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
anchor = 'REXCVAR_DEFINE_BOOL(ht_msaa_clear_remap, true, "GPU",'
assert s.count(anchor) == 1
s = s.replace(anchor,
              'REXCVAR_DEFINE_BOOL(ht_texture_set_cache, true, "GPU",\n'
              '                    "Reuse a texture descriptor set written earlier in the frame with identical inputs");\n'
              'REXCVAR_DEFINE_BOOL(ht_texture_set_cache_verify, false, "GPU",\n'
              '                    "Test: write every set anyway and compare the cached inputs with the fresh ones");\n'
              'REXCVAR_DEFINE_BOOL(ht_texture_set_cache_verify_break, false, "GPU",\n'
              '                    "Test (positive control): perturb one cached input before comparing");\n'
              + anchor, 1)
cp.write_text(s)

patch(cp, """  // Write the new descriptor sets.

  // Consecutive bindings updated via a single VkWriteDescriptorSet must have""",
      """  // ht_texture_set_cache: a set written earlier in this frame with the same layout and
  // the same image infos is reused instead of allocating and writing a new one.
  for (uint32_t ht_i = 0; ht_i < 2; ++ht_i) {
    if (!ht_texture_set_pending_[ht_i]) continue;
    // The previous draw added an entry but never wrote its set (allocation failure): drop it.
    auto ht_range = ht_texture_set_cache_[ht_i].equal_range(ht_texture_set_pending_key_[ht_i]);
    for (auto ht_it = ht_range.first; ht_it != ht_range.second;) {
      ht_it = ht_it->second.set == VK_NULL_HANDLE ? ht_texture_set_cache_[ht_i].erase(ht_it) : std::next(ht_it);
    }
    ht_texture_set_pending_[ht_i] = false;
  }
  if (REXCVAR_GET(ht_texture_set_cache)) {
    if (ht_texture_set_cache_frame_ != frame_current_) {
      ht_texture_set_cache_[0].clear();
      ht_texture_set_cache_[1].clear();
      ht_texture_set_cache_frame_ = frame_current_;
    }
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
    const bool ht_verify = REXCVAR_GET(ht_texture_set_cache_verify);
    for (uint32_t ht_i = 0; ht_i < 2; ++ht_i) {
      HtStage& st = ht_stages[ht_i];
      if (!*st.write) {
        continue;
      }
      const VkDescriptorImageInfo* tex = descriptor_write_image_info_.data() + st.texture_offset;
      const VkDescriptorImageInfo* smp = descriptor_write_image_info_.data() + st.sampler_offset;
      // FNV-1a over the layout and every input of the set.
      uint64_t key = UINT64_C(14695981039346656037);
      auto mix = [&key](uint64_t v) { key = (key ^ v) * UINT64_C(1099511628211); };
      mix(uint64_t(reinterpret_cast<uintptr_t>(st.layout)));
      mix(st.texture_count | (uint64_t(st.sampler_count) << 32));
      for (uint32_t k = 0; k < st.texture_count; ++k) {
        mix(uint64_t(reinterpret_cast<uintptr_t>(tex[k].imageView)));
        mix(uint64_t(tex[k].imageLayout));
      }
      for (uint32_t k = 0; k < st.sampler_count; ++k) {
        mix(uint64_t(reinterpret_cast<uintptr_t>(smp[k].sampler)));
      }
      auto same_as = [&](const HtTextureSetEntry& e) {
        if (e.layout != st.layout || e.texture_count != st.texture_count ||
            e.sampler_count != st.sampler_count || e.set == VK_NULL_HANDLE) {
          return false;
        }
        for (uint32_t k = 0; k < st.texture_count; ++k) {
          if (e.infos[k].imageView != tex[k].imageView || e.infos[k].imageLayout != tex[k].imageLayout) {
            return false;
          }
        }
        for (uint32_t k = 0; k < st.sampler_count; ++k) {
          if (e.infos[st.texture_count + k].sampler != smp[k].sampler) {
            return false;
          }
        }
        return true;
      };
      const HtTextureSetEntry* found = nullptr;
      auto range = ht_texture_set_cache_[ht_i].equal_range(key);
      for (auto it = range.first; it != range.second; ++it) {
        if (same_as(it->second)) {
          found = &it->second;
          break;
        }
      }
      if (found && !ht_verify) {
        *st.write = false;
        current_graphics_descriptor_sets_[st.set_index] = found->set;
        current_graphics_descriptor_set_values_up_to_date_ |= UINT32_C(1) << st.set_index;
        ++ht_texture_sets_reused_;
        continue;
      }
      if (found && ht_verify) {
        // Proof mode: the set is written as before; the cached entry must have exactly the
        // inputs being written now (the break knob perturbs a copy of it).
        HtTextureSetEntry probe = *found;
        if (REXCVAR_GET(ht_texture_set_cache_verify_break) && !probe.infos.empty()) {
          probe.infos[0].imageLayout = VkImageLayout(int(probe.infos[0].imageLayout) ^ 1);
          probe.infos[0].sampler = probe.infos[0].sampler ? VK_NULL_HANDLE : probe.infos[0].sampler;
        }
        ++ht_texture_sets_verified_;
        bool match = probe.infos.size() == size_t(st.texture_count) + st.sampler_count;
        for (uint32_t k = 0; match && k < st.texture_count; ++k) {
          match = probe.infos[k].imageView == tex[k].imageView && probe.infos[k].imageLayout == tex[k].imageLayout;
        }
        for (uint32_t k = 0; match && k < st.sampler_count; ++k) {
          match = probe.infos[st.texture_count + k].sampler == smp[k].sampler;
        }
        ht_texture_sets_mismatches_ += !match;
        continue;  // written below as usual
      }
      HtTextureSetEntry entry;
      entry.layout = st.layout;
      entry.texture_count = st.texture_count;
      entry.sampler_count = st.sampler_count;
      entry.infos.assign(tex, tex + st.texture_count);
      entry.infos.insert(entry.infos.end(), smp, smp + st.sampler_count);
      ht_texture_set_cache_[ht_i].emplace(key, std::move(entry));  // set filled once written
      ht_texture_set_pending_key_[ht_i] = key;
      ht_texture_set_pending_[ht_i] = true;
      ++ht_texture_sets_written_;
    }
  }

  // Write the new descriptor sets.

  // Consecutive bindings updated via a single VkWriteDescriptorSet must have""")
for stage, name in ((0, "Vertex"), (1, "Pixel")):
    patch(cp, f"""    write_descriptor_set_bits |= UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTextures{name};
    current_graphics_descriptor_sets_[SpirvShaderTranslator::kDescriptorSetTextures{name}] =
        write_textures[0].dstSet;
  }}""", f"""    write_descriptor_set_bits |= UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTextures{name};
    current_graphics_descriptor_sets_[SpirvShaderTranslator::kDescriptorSetTextures{name}] =
        write_textures[0].dstSet;
    if (ht_texture_set_pending_[{stage}]) {{
      // The entry added above for this draw (last one with this key) gets its set.
      auto ht_range = ht_texture_set_cache_[{stage}].equal_range(ht_texture_set_pending_key_[{stage}]);
      for (auto ht_it = ht_range.first; ht_it != ht_range.second; ++ht_it) {{
        if (ht_it->second.set == VK_NULL_HANDLE) {{
          ht_it->second.set = write_textures[0].dstSet;
          break;
        }}
      }}
      ht_texture_set_pending_[{stage}] = false;
    }}
  }}""")
# Report with the [ht-draw] line.
s = cp.read_text()
marker = '"[ht-draw] t='
assert s.count(marker) == 1, s.count(marker)
idx = s.index(marker)
stmt_start = s.rindex("REXGPU_WARN(", 0, idx)
s = s[:stmt_start] + """if (REXCVAR_GET(ht_texture_set_cache)) {
      REXGPU_WARN("[ht-bind] texture sets reused={} written={} verified={} mismatches={}",
                  ht_texture_sets_reused_, ht_texture_sets_written_, ht_texture_sets_verified_,
                  ht_texture_sets_mismatches_);
    }
    """ + s[stmt_start:]
cp.write_text(s)
print("patched texture set cache")
