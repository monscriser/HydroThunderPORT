#!/bin/bash
# Revert the temporary resolve diagnostics, back up, apply ht_direct_resolve, rebuild Windows.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
if [ -d /root/sdk-backup-measuredump ]; then
  cp -r /root/sdk-backup-measuredump/* /root/ht/sdk/
  mv /root/sdk-backup-measuredump /root/sdk-backup-measuredump.used
  rm -rf /root/sdk-backup-resolvetrace.used; [ -d /root/sdk-backup-resolvetrace ] && mv /root/sdk-backup-resolvetrace /root/sdk-backup-resolvetrace.used
fi
BK=/root/sdk-backup-dres
if [ ! -d "$BK" ]; then mkdir -p "$BK"; cp --parents include/rex/graphics/vulkan/render_target_cache.h src/graphics/vulkan/render_target_cache.cpp "$BK/"; fi
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_direct_resolve.py" /root/ht/sdk "$SRC/sdk-extra/ht_direct_resolve"
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED' | head -20
