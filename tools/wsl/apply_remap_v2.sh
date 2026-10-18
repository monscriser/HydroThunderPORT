#!/bin/bash
# One-off: apply the MSAA clear remap v2 (+ debug fields) and rebuild Windows.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
cp --parents include/rex/graphics/util/draw_extent_estimator.h src/graphics/util/draw_extent_estimator.cpp /root/sdk-backup-blit/ 2>/dev/null || true
python3 -I "$SRC/tools/wsl/patch_msaa_clear_remap_v2.py" /root/ht/sdk
python3 -I - <<'EOF'
from pathlib import Path
p = Path('/root/ht/sdk/src/graphics/vulkan/command_processor.cpp')
s = p.read_text()
if 'const={}' not in s:
    s = s.replace('owner={}",', 'owner={} const={}",', 1)
    old = '''            render_target_cache_->HtClearCanUse1x(normalized_depth_control, ht_color_mask,
                                                  *vertex_shader));
      }'''
    new = '''            render_target_cache_->HtClearCanUse1x(normalized_depth_control, ht_color_mask,
                                                  *vertex_shader),
            render_target_cache_->HtRectangleExportsConstant(*vertex_shader, interpolator_mask));
      }'''
    assert old in s
    p.write_text(s.replace(old, new, 1))
print('debug fields ok')
EOF
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED' | head -20
