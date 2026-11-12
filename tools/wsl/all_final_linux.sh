#!/usr/bin/env bash
# FINAL BUILD Linux: build (launcher) -> all verifiers -> options test -> launcher screenshots.
set -euo pipefail
D=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7/tools/wsl
for s in build_final_linux linux_final test_options_final launcher_final_linux; do tr -d '\r' < "$D/$s.sh" > "/root/$s.sh"; done
LAUNCHER=1 bash /root/build_final_linux.sh
bash /root/linux_final.sh pgo
bash /root/test_options_final.sh
bash /root/launcher_final_linux.sh
