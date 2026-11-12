#!/usr/bin/env bash
# DIAGNOSTIC Linux build (1.1.0 + per-track telemetry), PGO code like the 1.1.0 PGO variant.
#  - patch_renderdoc_noload.py: real fix (RenderDoc overlay on SteamOS), stays in the SDK.
#  - diag/patch_diag_hooks.py: telemetry hooks, DIAGNOSTIC ONLY -> undone by restore_after_diag.sh.
# usage: build_diag_linux.sh
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
OUT="$HP/diag-110-linux"
export PATH="/root/ht/toolbin:/usr/bin:/bin"
BK=/root/ht/backups/diag-110
if [ ! -d "$BK" ]; then  # keep the FIRST backup (= SDK of the 1.1.0 release)
  mkdir -p "$BK"
  (cd /root/ht/sdk; cp --parents src/filesystem/virtual_file_system.cpp src/graphics/vulkan/command_processor.cpp \
     src/ui/renderdoc_api.cpp "$BK/")
  cp /root/ht/src/app/CMakeLists.txt "$BK/app-CMakeLists.txt"
  cp /root/ht/src/app/src/main.cpp "$BK/app-main.cpp"; cp /root/ht/src/app/src/ht_version.h "$BK/app-ht_version.h"
fi
H=include/rex/graphics/vulkan/command_processor.h
[ -f "$BK/$H" ] || (cd /root/ht/sdk; cp --parents "$H" "$BK/")
# Start from the release sources of the diag-only files, then: RenderDoc fix, per-draw GPU
# timing + barrier/shader trace (measurement-only patches from the handheld kit), diag hooks.
(cd /root/ht/sdk; cp "$BK/src/filesystem/virtual_file_system.cpp" src/filesystem/virtual_file_system.cpp
 cp "$BK/src/graphics/vulkan/command_processor.cpp" src/graphics/vulkan/command_processor.cpp; cp "$BK/$H" "$H")
python3 -I "$SRC/tools/wsl/patch_renderdoc_noload.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_draw_timing.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_barrier_trace.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/diag/patch_diag_hooks.py" /root/ht/sdk
for f in main.cpp ht_version.h ht_diag.h; do cp "$SRC/app/src/$f" /root/ht/src/app/src/$f; done
cp "$SRC/app/CMakeLists.txt" /root/ht/src/app/CMakeLists.txt
cd /root/ht/build-linux
grep -q 'HT_PGO:STRING=USE' CMakeCache.txt
cmake -DHT_DIAG=ON . > cmake-diag.log 2>&1
start=$(date +%s)
bash /root/ht/rebuild900-driver.sh 15 linux | tail -3
echo "build seconds: $(( $(date +%s) - start ))"
grep -c '1.1.0-beta-diag' hydrothunder
mkdir -p "$OUT"
cp /root/ht/build-linux/hydrothunder /root/ht/build-linux/librexruntime.so \
   /root/ht/build-linux/librexgpu-xenos.so /root/ht/sdk/out/linux-amd64/libamd_fidelityfx_vk.so "$OUT/"
sha256sum "$OUT"/* | cut -c1-16
