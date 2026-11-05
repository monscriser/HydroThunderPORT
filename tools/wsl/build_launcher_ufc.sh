#!/usr/bin/env bash
# Launcher in the UFC layout + user_data folder choice: copies the changed app files and
# rebuilds (win|linux). SDK stays as it is (9.1.0 + B56/B57).
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
T="${1:-win}"
OUT="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port/${2:-cand-launcher-$T}"
for f in ht_version.h main.cpp ht_keyboard_binds.h ht_setup_wizard.cpp ht_library_paths.h ht_save_backup.h ht_content_install.cpp ht_app_common.h ht_user_data.h; do
  cp "$SRC/app/src/$f" /root/ht/src/app/src/$f
done
export PATH="/root/ht/toolbin:/usr/bin:/bin"
bash /root/ht/rebuild900-driver.sh 15 "$T" | tail -3
mkdir -p "$OUT"
if [ "$T" = win ]; then
  cp /root/ht/build-win/hydrothunder.exe /root/ht/sdk/out/win-amd64/librexruntime.dll /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll "$OUT/"
else
  cp /root/ht/build-linux/hydrothunder /root/ht/sdk/out/linux-amd64/*.so "$OUT/"
fi
sha256sum "$OUT"/* | cut -c1-16
