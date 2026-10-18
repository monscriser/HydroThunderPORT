#!/usr/bin/env bash
# 8.9.2 = 8.9.1 + ht_zpd_exact_key (lights through walls) + ht_zpd_flush off by default.
# SDK: shared_memory.cpp from /root/sdk-backup-reqrange + patch_request_range_fast.py,
# command_processor.{h,cpp} from /root/sdk-backup-zpdkey + patch_zpd_exact_key.py.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/outputs/otimizacao-8.9/build-892
mkdir -p "$OUT"
BACKUP="/root/ht/backups/revision892-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$BACKUP"
cp -p /root/ht/build-win/hydrothunder.exe /root/ht/build-linux/hydrothunder "$BACKUP/"
cp -p /root/ht/src/app/src/ht_version.h "$BACKUP/"
cp "$SRC/app/src/ht_version.h" /root/ht/src/app/src/ht_version.h
for f in main.cpp ht_setup_wizard.cpp ht_keyboard_binds.h; do cmp -s "$SRC/app/src/$f" "/root/ht/src/app/src/$f"; done
cp -r /root/sdk-backup-reqrange/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_request_range_fast.py" /root/ht/sdk
cp -r /root/sdk-backup-zpdkey/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_zpd_exact_key.py" /root/ht/sdk
! grep -q "g_ht_rr_ticks\|ht-zpd-stats\|ht-tdraw" /root/ht/sdk/src/graphics/shared_memory.cpp /root/ht/sdk/src/graphics/vulkan/command_processor.cpp
grep -q 'REXCVAR_DEFINE_BOOL(ht_zpd_flush, false' /root/ht/sdk/src/graphics/vulkan/command_processor.cpp
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/ht/rebuild892-driver.sh
bash /root/ht/rebuild892-driver.sh 15 win | tee "$OUT/windows-summary.log"
bash /root/ht/rebuild892-driver.sh 15 linux | tee "$OUT/linux-summary.log"
printf '%s\n' "$BACKUP" > "$OUT/backup.txt"
sha256sum /root/ht/build-win/hydrothunder.exe /root/ht/sdk/out/win-amd64/*.dll /root/ht/build-linux/hydrothunder /root/ht/sdk/out/linux-amd64/*.so | tee "$OUT/build-hashes.txt"
