#!/usr/bin/env bash
# build -> llvmpipe smoke (+RenderDoc proof) -> AppImage. usage: all_diag_linux.sh [smoke seconds]
set -euo pipefail
D=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7/tools/wsl/diag
for s in build_diag_linux stage_diag_linux package_diag_linux; do tr -d '\r' < "$D/$s.sh" > "/root/$s.sh"; done
bash /root/build_diag_linux.sh
bash /root/stage_diag_linux.sh "${1:-200}"
bash /root/package_diag_linux.sh
