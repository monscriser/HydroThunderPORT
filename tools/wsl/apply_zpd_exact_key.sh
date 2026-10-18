#!/bin/bash
# ht_zpd_exact_key on the current SDK (8.9.1). Backs up the two files once
# (/root/sdk-backup-zpdkey) and always applies on that pristine copy. Rebuilds Windows.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
BK=/root/sdk-backup-zpdkey
FILES="include/rex/graphics/vulkan/command_processor.h src/graphics/vulkan/command_processor.cpp"
if [ ! -d "$BK" ]; then mkdir -p "$BK"; cp --parents $FILES "$BK/"; fi
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_zpd_exact_key.py" /root/ht/sdk
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -30
sha256sum /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll
