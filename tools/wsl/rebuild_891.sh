#!/usr/bin/env bash
# 8.9.1: app (exit fix, launcher Controls tab, Saves/Steam layout, version) + SDK
# ht_request_range_fast (patch_request_range_fast.py on the pristine files kept in
# /root/sdk-backup-reqrange). Builds Windows and Linux and records the hashes.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/outputs/otimizacao-8.9/build-891
mkdir -p "$OUT"
BACKUP="/root/ht/backups/revision891-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$BACKUP"
cp -p /root/ht/build-win/hydrothunder.exe /root/ht/build-linux/hydrothunder "$BACKUP/"
for f in main.cpp ht_version.h ht_setup_wizard.cpp ht_keyboard_binds.h; do
  [ -f "/root/ht/src/app/src/$f" ] && cp -p "/root/ht/src/app/src/$f" "$BACKUP/"
  cp "$SRC/app/src/$f" "/root/ht/src/app/src/$f"
done
# SDK = pristine + fast path only (no measurement timer).
cp -r /root/sdk-backup-reqrange/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_request_range_fast.py" /root/ht/sdk
! grep -q g_ht_rr_ticks /root/ht/sdk/src/graphics/shared_memory.cpp
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/ht/rebuild891-driver.sh
bash /root/ht/rebuild891-driver.sh 15 win | tee "$OUT/windows-summary.log"
bash /root/ht/rebuild891-driver.sh 15 linux | tee "$OUT/linux-summary.log"
printf '%s\n' "$BACKUP" > "$OUT/backup.txt"
sha256sum /root/ht/build-win/hydrothunder.exe /root/ht/sdk/out/win-amd64/*.dll /root/ht/build-linux/hydrothunder /root/ht/sdk/out/linux-amd64/*.so | tee "$OUT/build-hashes.txt"
