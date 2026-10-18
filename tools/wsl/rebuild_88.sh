#!/usr/bin/env bash
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/outputs
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP="/root/ht/backups/revision88-$STAMP"
mkdir -p "$BACKUP" "$OUT/otimizacao-8.8/build-88"
for f in main.cpp ht_spin_backoff.h ht_version.h; do
  if [ -f "/root/ht/src/app/src/$f" ]; then cp -p "/root/ht/src/app/src/$f" "$BACKUP/$f"; fi
  cp "$SRC/app/src/$f" "/root/ht/src/app/src/$f"
done
cp -p /root/ht/build-win/hydrothunder.exe "$BACKUP/hydrothunder-before.exe"
cp -p /root/ht/build-linux/hydrothunder "$BACKUP/hydrothunder-before-linux"
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/ht/rebuild88-driver.sh
bash /root/ht/rebuild88-driver.sh 15 win | tee "$OUT/otimizacao-8.8/build-88/windows-summary.log"
cp /root/ht/build-win/build2.log "$OUT/otimizacao-8.8/build-88/windows-build.log"
bash /root/ht/rebuild88-driver.sh 15 linux | tee "$OUT/otimizacao-8.8/build-88/linux-summary.log"
cp /root/ht/build-linux/build2.log "$OUT/otimizacao-8.8/build-88/linux-build.log"
printf '%s\n' "$BACKUP" > "$OUT/otimizacao-8.8/build-88/backup.txt"
sha256sum /root/ht/build-win/hydrothunder.exe /root/ht/build-linux/hydrothunder > "$OUT/otimizacao-8.8/build-88/build-hashes.txt"