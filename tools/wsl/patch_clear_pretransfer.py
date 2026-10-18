"""Experiment (cvar ht_skip_clear_color_pretransfer, default false): drop the
color ownership transfer into a render target that a Direct3D-style clear is
about to overwrite completely (3-vertex rectangle list in screen space, full
color write mask, no blending). Depth/stencil transfers are kept: the game's
clears leave stencil untouched.

usage: python3 patch_clear_pretransfer.py /root/ht/sdk
"""
import sys
from pathlib import Path

rc = Path(sys.argv[1]) / "src/graphics/pipeline/render_target/cache.cpp"
s = rc.read_text()
if "ht_skip_clear_color_pretransfer" not in s:
    anchor = "REXCVAR_DEFINE_BOOL(mrt_edram_used_range_clamp_to_min"
    assert anchor in s
    s = s.replace(anchor, '''REXCVAR_DEFINE_BOOL(ht_skip_clear_color_pretransfer, false, "GPU",
                    "Experimental: skip copying old color into a render target that a "
                    "full-screen clear rectangle overwrites");

''' + anchor, 1)
    old = '''  if (interlock_barrier_only) {
    // No copying transfers or render target bindings - only needed the barrier.
    return true;
  }
'''
    new = old + '''
  if (REXCVAR_GET(ht_skip_clear_color_pretransfer)) {
    // Direct3D clear: auto-indexed 3-vertex rectangle list, screen-space
    // positions (no viewport transform), blending replaced by the source.
    const uint32_t initiator = regs[XE_GPU_REG_VGT_DRAW_INITIATOR];
    const bool clear_rect = (initiator & 0x3F) == uint32_t(xenos::PrimitiveType::kRectangleList) &&
                            ((initiator >> 6) & 0x3) == 0x2 && (initiator >> 16) == 3 &&
                            (regs[XE_GPU_REG_PA_CL_VTE_CNTL] & 0x3F) == 0 &&
                            !(regs[XE_GPU_REG_RB_COLORCONTROL] & 0x7);  // no alpha test
    if (clear_rect) {
      for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
        const uint32_t b = edram_bases_sorted[i].second;
        if (b == 0 || last_update_transfers_[b].empty()) continue;
        const uint32_t color_index = b - 1;
        const uint32_t write_mask = (normalized_color_mask >> (color_index * 4)) & 0xF;
        const uint32_t blend = regs[reg::RB_BLENDCONTROL::rt_register_indices[color_index]];
        // ONE * src + ZERO * dst for color and alpha.
        if (write_mask == 0xF && (blend & 0x1FFF) == 0x0001 && ((blend >> 16) & 0x1FFF) == 0x0001) {
          last_update_transfers_[b].clear();
        }
      }
    }
  }
'''
    assert old in s
    s = s.replace(old, new, 1)
    rc.write_text(s)
print("patched")
