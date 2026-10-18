#!/bin/bash
# MEASUREMENT ONLY. Back up the SDK files (first run), apply patch_measure_reqrange.py on the
# pristine copy, rebuild Windows. Does NOT copy the DLL anywhere (the caller does, after
# backing up the tested DLL). Revert: cp -r /root/sdk-backup-reqrange/* /root/ht/sdk/ + rebuild.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
BK=/root/sdk-backup-reqrange
FILES="src/graphics/shared_memory.cpp src/graphics/vulkan/command_processor.cpp"
if [ ! -d "$BK" ]; then mkdir -p "$BK"; cp --parents $FILES "$BK/"; fi
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_measure_reqrange.py" /root/ht/sdk
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -30
sha256sum /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll /root/ht/sdk/out/win-amd64/librexruntime.dll
