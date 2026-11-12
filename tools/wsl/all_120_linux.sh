#!/usr/bin/env bash
# 1.2.0 final: build (with launcher) -> all verifiers (llvmpipe) -> Linux launcher screenshots -> AppImage.
set -euo pipefail
D=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7/tools/wsl
for s in build_120_linux linux_120 launcher_120_linux package_120; do tr -d '\r' < "$D/$s.sh" > "/root/$s.sh"; done
LAUNCHER=1 bash /root/build_120_linux.sh
bash /root/linux_120.sh pgo
bash /root/launcher_120_linux.sh
bash /root/package_120.sh
