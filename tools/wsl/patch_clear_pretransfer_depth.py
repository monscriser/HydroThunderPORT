"""Experiment (after patch_clear_pretransfer.py): with ht_skip_clear_color_pretransfer
also drop the depth/stencil pre-transfer when the clear writes depth (ALWAYS)
and stencil completely (ALWAYS + REPLACE, write mask 0xFF, no back-face stencil).
Upper bound measurement only; coverage is not checked.

usage: python3 patch_clear_pretransfer_depth.py /root/ht/sdk
"""
import sys
from pathlib import Path

rc = Path(sys.argv[1]) / "src/graphics/pipeline/render_target/cache.cpp"
s = rc.read_text()
old = """        if (b == 0 || last_update_transfers_[b].empty()) continue;"""
new = """        if (last_update_transfers_[b].empty()) continue;
        if (b == 0) {
          const auto dc = normalized_depth_control;
          const uint32_t stencil_write_mask = (regs[XE_GPU_REG_RB_STENCILREFMASK] >> 16) & 0xFF;
          if (dc.z_enable && dc.z_write_enable && dc.zfunc == xenos::CompareFunction::kAlways &&
              dc.stencil_enable && !dc.backface_enable &&
              dc.stencilfunc == xenos::CompareFunction::kAlways &&
              dc.stencilzpass == xenos::StencilOp::kReplace && stencil_write_mask == 0xFF) {
            last_update_transfers_[b].clear();
          }
          continue;
        }"""
if "stencil_write_mask" not in s:
    assert old in s
    rc.write_text(s.replace(old, new, 1))
print("patched")
