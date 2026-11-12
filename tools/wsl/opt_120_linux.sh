#!/usr/bin/env bash
set -euo pipefail
D=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7/tools/wsl
for s in build_120_linux linux_120 test_options_linux; do tr -d '\r' < "$D/$s.sh" > "/root/$s.sh"; done
LAUNCHER=1 bash /root/build_120_linux.sh
bash /root/linux_120.sh pgo
bash /root/test_options_linux.sh
