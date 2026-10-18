#!/bin/bash
# Apply ht_texture_direct to /root/ht/sdk (backup first) and rebuild Windows.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
BK=/root/sdk-backup-direct
cd /root/ht/sdk
if [ ! -d "$BK" ]; then
  mkdir -p "$BK"
  cp --parents include/rex/ui/vulkan/functions/device_1_0.inc include/rex/graphics/vulkan/texture_cache.h src/graphics/vulkan/texture_cache.cpp "$BK/"
fi
python3 -I "$SRC/tools/wsl/patch_texture_direct.py" /root/ht/sdk "$SRC/sdk-extra/ht_texture_direct"
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | tail -15
ls -la --time-style=+%T /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll
