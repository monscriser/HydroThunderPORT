#!/bin/bash
# Back up, apply ht_resolve_to_texture (from the pristine backup on re-runs), rebuild Windows.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cd /root/ht/sdk
BK=/root/sdk-backup-rtt
FILES="include/rex/graphics/pipeline/texture/cache.h include/rex/graphics/vulkan/texture_cache.h src/graphics/vulkan/texture_cache.cpp include/rex/graphics/vulkan/render_target_cache.h src/graphics/vulkan/render_target_cache.cpp"
if [ ! -d "$BK" ]; then mkdir -p "$BK"; cp --parents $FILES "$BK/"; fi
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_resolve_texture.py" /root/ht/sdk "$SRC/sdk-extra/ht_resolve_texture"
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -30
ls -la --time-style=+%T /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll
