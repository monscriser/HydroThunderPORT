#!/bin/bash
# Temporary diagnostic: back up the 2 files, apply patch_rect_debug.py, rebuild Windows.
# Revert: cp -r /root/sdk-backup-rectdebug/* /root/ht/sdk/
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
BK=/root/sdk-backup-rectdebug
cd /root/ht/sdk
if [ ! -d "$BK" ]; then mkdir -p "$BK"; cp --parents src/graphics/util/draw_extent_estimator.cpp src/graphics/pipeline/render_target/cache.cpp "$BK/"; fi
python3 -I "$SRC/tools/wsl/patch_rect_debug.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_rect_debug2.py" /root/ht/sdk
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED' | head
