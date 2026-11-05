#!/usr/bin/env bash
# Merges the PGO profiles and builds the optimized binary. usage: pgo_merge_build.sh <name> <profraw names...>
set -euo pipefail
NAME="$1"; shift
P="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port/pgo-prof"
FILES=(); for f in "$@"; do FILES+=("$P/$f.profraw"); done
/root/ht/llvm-mingw/bin/llvm-profdata merge -o "/root/ht/pgo-$NAME.profdata" "${FILES[@]}"
ls -la "/root/ht/pgo-$NAME.profdata"
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
tr -d '\r' < "$SRC/tools/wsl/build_pgo.sh" > /root/bpgo.sh
bash /root/bpgo.sh USE "/root/ht/pgo-$NAME.profdata" "pgo-USE-$NAME-windows"
