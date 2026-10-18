#!/bin/bash
# Revert the temporary rect diagnostics, back up, apply ht_clear_transfer_cutout, rebuild Windows.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
if [ -d /root/sdk-backup-rectdebug ]; then cp -r /root/sdk-backup-rectdebug/* /root/ht/sdk/; mv /root/sdk-backup-rectdebug /root/sdk-backup-rectdebug.used; fi
BK=/root/sdk-backup-cutout
if [ ! -d "$BK" ]; then mkdir -p "$BK"; cp --parents include/rex/graphics/pipeline/render_target/cache.h include/rex/graphics/vulkan/render_target_cache.h src/graphics/vulkan/command_processor.cpp src/graphics/vulkan/render_target_cache.cpp "$BK/"; fi
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_clear_cutout.py" /root/ht/sdk
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED' | head -20
