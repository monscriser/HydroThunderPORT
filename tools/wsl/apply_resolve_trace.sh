#!/bin/bash
# Temporary: back up render_target_cache.cpp, add the dump timer, rebuild. Revert: revert_measure_dump.sh
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
mkdir -p /root/sdk-backup-resolvetrace
cp --parents src/graphics/vulkan/render_target_cache.cpp /root/sdk-backup-resolvetrace/
python3 -I "$SRC/tools/wsl/patch_resolve_trace.py" /root/ht/sdk
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED' | head
