#!/bin/bash
# Copy the version header into the WSL app tree and rebuild Windows + Linux.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
cp "$SRC/app/src/ht_version.h" /root/ht/src/app/src/ht_version.h
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win | tail -1
bash /root/rw.sh 15 linux | tail -1
strings /root/ht/build-win/hydrothunder.exe | grep -m1 'revision-8' || true
