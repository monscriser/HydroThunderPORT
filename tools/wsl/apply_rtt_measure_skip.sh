#!/bin/bash
# Measurement only: on top of the current SDK (8.6 + draw timing), add the skip-buffer cvar,
# rebuild Windows and copy the DLL to the test copy.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
BK=/root/sdk-backup-rttmeasure
if [ ! -d "$BK" ]; then
  mkdir -p "$BK"
  cp --parents src/graphics/vulkan/render_target_cache.cpp src/graphics/shaders/vulkan_spirv/ht_rtt_*_cs.h "$BK/"
fi
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_rtt_measure_skip.py" /root/ht/sdk "$SRC/sdk-extra/ht_resolve_texture"
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -30
cp /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll /mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test/rexgpu-xenos.dll
