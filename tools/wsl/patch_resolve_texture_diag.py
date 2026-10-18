"""Diagnostic for ht_resolve_to_texture (only with HT_GPU_DIAG=1): once per destination,
log why a direct resolve did not write a texture, and the candidate textures
overlapping its memory.

usage: python3 patch_resolve_texture_diag.py /root/ht/sdk
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def patch(path, old, new, count=1):
    s = path.read_text()
    assert s.count(old) == count, (path, s.count(old), old[:120])
    path.write_text(s.replace(old, new))


tc_h = root / "include/rex/graphics/vulkan/texture_cache.h"
patch(tc_h, "  void HtRttFinish(const HtRttTarget& target, bool verify);\n",
      "  void HtRttFinish(const HtRttTarget& target, bool verify);\n"
      "  void HtRttLogCandidates(uint32_t start, uint32_t length);\n")
tc = root / "src/graphics/vulkan/texture_cache.cpp"
patch(tc, "void VulkanTextureCache::HtRttFinish(", r'''void VulkanTextureCache::HtRttLogCandidates(uint32_t start, uint32_t length) {
  start &= 0x1FFFFFFF;
  for (VulkanTexture* texture : ht_rtt_candidates_) {
    const TextureKey& key = texture->key();
    const uint32_t base = key.base_page << 12;
    if (base >= start + length || base + texture->GetGuestBaseSize() <= start) continue;
    REXGPU_WARN("[ht-rtt-why]   candidate base={:08X} size={} {}x{} pitch32={} fmt={} endian={} "
                "scaled={} conv={} outdated={}",
                base, texture->GetGuestBaseSize(), key.GetWidth(), key.GetHeight(), key.pitch,
                uint32_t(key.format), uint32_t(key.endianness), uint32_t(key.scaled_resolve),
                uint32_t(texture->ht_rtt_conv_), texture->outdated_mask());
  }
}

void VulkanTextureCache::HtRttFinish(''')

vk = root / "src/graphics/vulkan/render_target_cache.cpp"
patch(vk, """              } else {
                ++ht_rtt_stats_[1];
              }""", """              } else {
                ++ht_rtt_stats_[1];
                static std::vector<uint64_t> ht_why_seen;
                const uint64_t ht_why_key =
                    uint64_t(resolve_info.copy_dest_base) |
                    (uint64_t(resolve_info.coordinate_info.width_div_8) << 32) |
                    (uint64_t(resolve_info.height_div_8) << 48);
                if (HtGpuDiag() && ht_why_seen.size() < 64 &&
                    std::find(ht_why_seen.begin(), ht_why_seen.end(), ht_why_key) ==
                        ht_why_seen.end()) {
                  ht_why_seen.push_back(ht_why_key);
                  REXGPU_WARN("[ht-rtt-why] no texture: dest_base={:08X} extent={:08X}+{} "
                              "pitch32={} {}x{} off8={},{} fmt={} endian={}",
                              resolve_info.copy_dest_base, resolve_info.copy_dest_extent_start,
                              resolve_info.copy_dest_extent_length,
                              uint32_t(resolve_info.copy_dest_coordinate_info.pitch_aligned_div_32),
                              resolve_info.coordinate_info.width_div_8 << 3,
                              resolve_info.height_div_8 << 3,
                              uint32_t(resolve_info.copy_dest_coordinate_info.offset_x_div_8),
                              uint32_t(resolve_info.copy_dest_coordinate_info.offset_y_div_8),
                              uint32_t(resolve_info.copy_dest_info.copy_dest_format),
                              uint32_t(resolve_info.copy_dest_info.copy_dest_endian));
                  texture_cache.HtRttLogCandidates(resolve_info.copy_dest_extent_start,
                                                   resolve_info.copy_dest_extent_length);
                }
              }""")
print("patched diag")
