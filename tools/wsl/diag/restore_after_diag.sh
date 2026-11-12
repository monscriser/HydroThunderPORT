#!/usr/bin/env bash
# Undo the DIAGNOSTIC-only SDK hooks after build_diag_linux.sh; keeps the RenderDoc fix
# (patch_renderdoc_noload.py, a real fix for the next release). build-linux back to HT_DIAG=OFF.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
BK=/root/ht/backups/diag-110
export PATH="/root/ht/toolbin:/usr/bin:/bin"
cd /root/ht/sdk
cp "$BK/src/filesystem/virtual_file_system.cpp" src/filesystem/virtual_file_system.cpp
cp "$BK/src/graphics/vulkan/command_processor.cpp" src/graphics/vulkan/command_processor.cpp
cp "$BK/include/rex/graphics/vulkan/command_processor.h" include/rex/graphics/vulkan/command_processor.h
rm -f include/rex/ht_diag_hooks.h
python3 -I "$SRC/tools/wsl/patch_renderdoc_noload.py" /root/ht/sdk
! grep -rq "ht_diag hooks" src include
cd /root/ht/build-linux
cmake -DHT_DIAG=OFF . > cmake-diag-off.log 2>&1
bash /root/ht/rebuild900-driver.sh 15 linux | tail -3
grep -c '1.1.0-beta-pgo' hydrothunder
echo "SDK: diag hooks removed, RenderDoc fix kept; build-linux = 1.1.0 PGO + RenderDoc fix"
