#!/usr/bin/env bash
# FINAL BUILD Linux PGO (1.2.0 + shader pre-compilation of every track + log option): SDK = 1.1.0 release sources + RenderDoc attach-only + WaitMultiple precise
# sleep + texture descriptor set cache; app = current sources (new launcher when present). HT_DIAG=OFF.
# usage: build_120_linux.sh
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
OUT="$HP/final-build-linux-pgo"
export PATH="/root/ht/toolbin:/usr/bin:/bin"
BK=/root/ht/backups/diag-110      # 1.1.0 release copies of the files touched below
BKW=/root/ht/backups/wm-measure   # 1.1.0 release threading_posix.cpp
cd /root/ht/sdk
cp "$BK/src/filesystem/virtual_file_system.cpp" src/filesystem/virtual_file_system.cpp
cp "$BK/src/graphics/vulkan/command_processor.cpp" src/graphics/vulkan/command_processor.cpp
cp "$BK/include/rex/graphics/vulkan/command_processor.h" include/rex/graphics/vulkan/command_processor.h
cp "$BKW/threading_posix.cpp" src/core/threading_posix.cpp
[ -f "$BK/src/ui/rex_app.cpp" ] || (cd /root/ht/sdk; cp --parents src/ui/rex_app.cpp src/graphics/vulkan/pipeline_cache.cpp "$BK/")
cp "$BK/src/ui/rex_app.cpp" src/ui/rex_app.cpp
cp "$BK/src/graphics/vulkan/pipeline_cache.cpp" src/graphics/vulkan/pipeline_cache.cpp
rm -f include/rex/ht_diag_hooks.h
python3 -I "$SRC/tools/wsl/patch_renderdoc_noload.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_waitmultiple_sleep.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_texture_set_cache.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_shader_precompile_logs.py" /root/ht/sdk
! grep -rq "ht_diag hooks\|ht_wm::\|ht_tq_pool_" src include
# Launcher files come from the launcher work only when LAUNCHER=1 (they are mirrored by it).
for f in "$SRC"/app/src/*.cpp "$SRC"/app/src/*.h; do
  case "$(basename "$f")" in ht_setup_wizard*|ht_launcher_*) [ "${LAUNCHER:-0}" = 1 ] || continue;; esac
  cmp -s "$f" "/root/ht/src/app/src/$(basename "$f")" || cp "$f" /root/ht/src/app/src/
done
cp "$SRC/app/CMakeLists.txt" /root/ht/src/app/CMakeLists.txt
cd /root/ht/build-linux
grep -q 'HT_PGO:STRING=USE' CMakeCache.txt
cmake -DHT_DIAG=OFF . > cmake-120.log 2>&1
bash /root/ht/rebuild900-driver.sh 15 linux | tail -3
grep -c 'final-build-pgo' hydrothunder
rm -rf "$OUT"; mkdir -p "$OUT"
# Real SDK outputs live in sdk/out/linux-amd64 (build-linux/*.so are copies refreshed only on relink).
cp /root/ht/build-linux/hydrothunder /root/ht/sdk/out/linux-amd64/librexruntime.so \
   /root/ht/sdk/out/linux-amd64/librexgpu-xenos.so /root/ht/sdk/out/linux-amd64/libamd_fidelityfx_vk.so "$OUT/"
strings "$OUT/librexgpu-xenos.so" | grep -c ht_texture_set_cache
strings "$OUT/librexruntime.so" | grep -c ht_wait_precise_sleep
sha256sum "$OUT"/* | cut -c1-16
