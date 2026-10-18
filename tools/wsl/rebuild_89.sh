#!/usr/bin/env bash
# 8.9: only app/src/main.cpp + ht_version.h changed (exit fix). Windows exe was built
# and proven before this script; this rebuilds Linux and records both hashes.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/outputs
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP="/root/ht/backups/revision89-$STAMP"
mkdir -p "$BACKUP" "$OUT/otimizacao-8.9/build-89"
for f in main.cpp ht_version.h; do
  cmp -s "$SRC/app/src/$f" "/root/ht/src/app/src/$f" || { echo "fonte $f diferente do WSL"; exit 1; }
done
cp -p /root/ht/build-linux/hydrothunder "$BACKUP/hydrothunder-before-linux"
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/ht/rebuild89-driver.sh
bash /root/ht/rebuild89-driver.sh 15 linux | tee "$OUT/otimizacao-8.9/build-89/linux-summary.log"
cp /root/ht/build-linux/build2.log "$OUT/otimizacao-8.9/build-89/linux-build.log"
printf '%s\n' "$BACKUP" > "$OUT/otimizacao-8.9/build-89/backup.txt"
sha256sum /root/ht/build-win/hydrothunder.exe /root/ht/build-linux/hydrothunder /root/ht/sdk/out/linux-amd64/*.so /root/ht/sdk/out/win-amd64/*.dll > "$OUT/otimizacao-8.9/build-89/build-hashes.txt"
cat "$OUT/otimizacao-8.9/build-89/build-hashes.txt"