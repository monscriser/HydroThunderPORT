#!/usr/bin/env bash
# Linux PGO build (same profile as Windows: same recompiled code). usage: build_pgo_linux.sh GEN|USE|OFF [profdata] [outdir]
# Reconfigures /root/ht/build-win with HT_PGO and rebuilds; OFF returns to the normal build.
set -euo pipefail
MODE="$1"; PROF="${2:-}"; OUTNAME="${3:-pgo-$MODE-linux}"
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port/$OUTNAME"
cp "$SRC/app/CMakeLists.txt" /root/ht/src/app/CMakeLists.txt
cp "$SRC/app/src/main.cpp" /root/ht/src/app/src/main.cpp
cp "$SRC/app/src/ht_version.h" /root/ht/src/app/src/ht_version.h
export PATH="/root/ht/toolbin:/usr/bin:/bin"
cd /root/ht/build-linux
if [ "$MODE" = USE ]; then cmake -DHT_PGO=USE -DHT_PGO_PROFILE="$PROF" . > cmake-pgo.log 2>&1
else cmake -DHT_PGO="$MODE" -DHT_PGO_PROFILE= . > cmake-pgo.log 2>&1; fi
start=$(date +%s)
bash /root/ht/rebuild900-driver.sh 15 linux | tail -2
echo "build seconds: $(( $(date +%s) - start ))"
mkdir -p "$OUT"
cp /root/ht/build-linux/hydrothunder /root/ht/sdk/out/linux-amd64/*.so "$OUT/"
sha256sum "$OUT"/* | cut -c1-16
