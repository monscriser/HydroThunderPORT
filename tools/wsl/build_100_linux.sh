#!/usr/bin/env bash
# 1.0.0 BETA Linux: Standard (HT_PGO=OFF) and PGO (HT_PGO=USE, Linux-native profile of both tracks:
# hydro-port/pgo-prof-linux/L1-asgard + L2-outra, recorded with tools/wsl/linux_pgo_train.sh).
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
/root/ht/llvm-mingw/bin/llvm-profdata merge -o /root/ht/pgo-linux-both.profdata "$HP/pgo-prof-linux/L1-asgard.profraw" "$HP/pgo-prof-linux/L2-outra.profraw"
ls -la /root/ht/pgo-linux-both.profdata
tr -d '\r' < "$SRC/tools/wsl/build_pgo_linux.sh" > /root/bpgol.sh
bash /root/bpgol.sh OFF '' final-100-linux-standard | tail -4
strings "$HP/final-100-linux-standard/hydrothunder" | grep -m1 '1.0.0-beta' || true
bash /root/bpgol.sh USE /root/ht/pgo-linux-both.profdata final-100-linux-pgo | tail -4
strings "$HP/final-100-linux-pgo/hydrothunder" | grep -m1 '1.0.0-beta' || true
python3 "$HP/pgocheck.py" "$HP/pgo-prof-linux/L2-outra.profraw" | tail -2
