"""Measurement only (cvar ht_texture_blit_measure_skip_copy, default false):
skip the final vkCmdCopyImage of the texture blit to see how much of texcopy=
is the image copy. Breaks the image - never enable outside a measurement.

usage: python3 patch_blit_measure.py /root/ht/sdk
"""
import sys
from pathlib import Path

tc = Path(sys.argv[1]) / "src/graphics/vulkan/texture_cache.cpp"
s = tc.read_text()
if "ht_texture_blit_measure_skip_copy" not in s:
    anchor = 'REXCVAR_DEFINE_BOOL(ht_texture_blit, true, "GPU",'
    assert anchor in s
    s = s.replace(anchor, 'REXCVAR_DEFINE_BOOL(ht_texture_blit_measure_skip_copy, false, "GPU",\n'
                  '                    "Measurement only: skip the blit image copy (breaks textures)");\n' + anchor, 1)
    old = """    command_buffer.CmdVkCopyImage(ht_blit->image, VK_IMAGE_LAYOUT_GENERAL, vulkan_texture.image(),
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region);"""
    assert old in s
    s = s.replace(old, "    if (!REXCVAR_GET(ht_texture_blit_measure_skip_copy))\n" + old, 1)
    tc.write_text(s)
print("patched")
