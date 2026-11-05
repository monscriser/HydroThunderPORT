#!/usr/bin/env bash
# 1.0.0 BETA Windows: Standard (HT_PGO=OFF) and PGO (HT_PGO=USE, Windows profile of both tracks).
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
tr -d '\r' < "$SRC/tools/wsl/build_pgo.sh" > /root/bpgo.sh
[ "${1:-}" = pgo ] || bash /root/bpgo.sh OFF '' final-100-win-standard | tail -3
[ "${1:-}" = pgo ] || strings "$HP/final-100-win-standard/hydrothunder.exe" | grep -m1 '1.0.0-beta' || true
bash /root/bpgo.sh USE /root/ht/pgo-both.profdata final-100-win-pgo | tail -3
strings "$HP/final-100-win-pgo/hydrothunder.exe" | grep -m1 '1.0.0-beta' || true
