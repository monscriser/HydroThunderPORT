"""Proof mode for ht_clear_transfer_cutout (cvar ht_clear_cutout_sentinel, off):
the area the transfers skip is filled with absurd values (bright color, depth 0.5,
huge float) using the engine's resolve-clear path. If the game's clear did not
overwrite every one of those pixels, garbage shows up on screen / in shadows.
usage: python3 patch_clear_cutout_sentinel.py /root/ht/sdk"""
import sys
from pathlib import Path

vk = Path(sys.argv[1]) / "src/graphics/vulkan/render_target_cache.cpp"
s = vk.read_text()
OLD_DECL = "  uint64_t ht_sentinel[1 + xenos::kMaxColorRenderTargets] = {};\n"
NEW_DECL = ("  // Depth 0.5 (24-bit depth << 8 | stencil): wrong for both depth directions.\n"
            "  uint64_t ht_sentinel[1 + xenos::kMaxColorRenderTargets] = {UINT64_C(0x80000000)};\n")
if "ht_clear_cutout_sentinel" not in s:
    old = 'REXCVAR_DEFINE_BOOL(ht_clear_transfer_cutout, true, "GPU",'
    assert old in s
    s = s.replace(old, 'REXCVAR_DEFINE_BOOL(ht_clear_cutout_sentinel, false, "GPU",\n'
                  '                    "Test only: fill the skipped area with garbage to prove the clear covers it");\n' + old, 1)
    old = """  PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, rts, transfers, nullptr,
                                   &cut);"""
    assert old in s
    s = s.replace(old, NEW_DECL + """  for (uint32_t i = 1; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    if (rts[i]) {
      ht_sentinel[i] = rts[i]->key().GetColorFormat() == xenos::ColorRenderTargetFormat::k_32_FLOAT
                           ? UINT64_C(0x7F7FFFFF)
                           : UINT64_C(0xFFFF00FFFFFF00FF);
    }
  }
  PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, rts, transfers,
                                   REXCVAR_GET(ht_clear_cutout_sentinel) ? ht_sentinel : nullptr,
                                   &cut);""", 1)
elif OLD_DECL in s:
    s = s.replace(OLD_DECL, NEW_DECL, 1)
vk.write_text(s)
print("patched")
