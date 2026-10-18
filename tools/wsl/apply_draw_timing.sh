#!/bin/bash
# Measurement only: back up, add the per-draw GPU timing of the traced frame, rebuild Windows,
# copy the DLL to the test copy. Revert with: cp -r /root/sdk-backup-drawtiming/* /root/ht/sdk/
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
BK=/root/sdk-backup-drawtiming
FILES="include/rex/graphics/vulkan/command_processor.h src/graphics/vulkan/command_processor.cpp"
if [ ! -d "$BK" ]; then mkdir -p "$BK"; cp --parents $FILES "$BK/"; fi
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_draw_timing.py" /root/ht/sdk
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -30
cp /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll /mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test/rexgpu-xenos.dll
