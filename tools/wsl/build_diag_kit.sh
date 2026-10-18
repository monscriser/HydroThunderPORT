#!/bin/bash
# MEASUREMENT ONLY: diagnostic rexgpu-xenos.dll for the handheld GPU kit = current SDK
# (8.9.1) + per-draw GPU timing + barrier trace + HT_TRACE_FRAME list. The SDK sources are
# restored right after the build (and the normal DLL rebuilt), so nothing leaks into releases.
# usage: build_diag_kit.sh <output dir as seen from WSL>
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT="$1"; mkdir -p "$OUT"
cd /root/ht/sdk
BK=/root/sdk-backup-diag891-$(date +%Y%m%d-%H%M%S)
FILES="include/rex/graphics/vulkan/command_processor.h src/graphics/vulkan/command_processor.cpp"
mkdir -p "$BK"; cp --parents $FILES "$BK/"
restore() { cp -r "$BK"/* /root/ht/sdk/; }
trap restore EXIT
python3 -I "$SRC/tools/wsl/patch_draw_timing.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_barrier_trace.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_trace_multi.py" /root/ht/sdk
# Optional extra measurement patches (file names in tools/wsl), e.g. patch_zpd_trace.py.
for extra in "${@:2}"; do python3 -I "$SRC/tools/wsl/$extra" /root/ht/sdk; done
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -30
cp /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll "$OUT/rexgpu-xenos-diagnostico.dll"
sha256sum "$OUT/rexgpu-xenos-diagnostico.dll"
restore; trap - EXIT
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -5
echo "SDK restaurado de $BK"
sha256sum /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll
