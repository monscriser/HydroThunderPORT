#!/usr/bin/env bash
# 1.1.0 BETA: SDK + patch_player_profiles.py (4 local profiles, renameable), app with the save
# editor / player names / anisotropic option. Builds Windows and Linux, Standard and PGO.
# usage: build_110.sh [win|linux|all]
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
W="${1:-all}"
grep -q storage_name_ /root/ht/sdk/include/rex/system/xam/user_profile.h || python3 -I "$SRC/tools/wsl/patch_player_profiles.py" /root/ht/sdk
for f in ht_version.h main.cpp ht_keyboard_binds.h ht_setup_wizard.cpp ht_library_paths.h ht_save_backup.h \
         ht_content_install.cpp ht_app_common.h ht_user_data.h ht_player_names.h ht_save_editor.h; do
  cmp -s "$SRC/app/src/$f" /root/ht/src/app/src/$f || cp "$SRC/app/src/$f" /root/ht/src/app/src/$f
done
if [ "$W" != linux ]; then
  tr -d '\r' < "$SRC/tools/wsl/build_pgo.sh" > /root/bpgo.sh
  bash /root/bpgo.sh OFF '' final-110-win-standard | tail -5
  bash /root/bpgo.sh USE /root/ht/pgo-both.profdata final-110-win-pgo | tail -5
  for v in standard pgo; do strings "$HP/final-110-win-$v/hydrothunder.exe" | grep -m1 '1.1.0-beta' || true; done
fi
if [ "$W" != win ]; then
  tr -d '\r' < "$SRC/tools/wsl/build_pgo_linux.sh" > /root/bpgol.sh
  bash /root/bpgol.sh OFF '' final-110-linux-standard | tail -5
  bash /root/bpgol.sh USE /root/ht/pgo-linux-both.profdata final-110-linux-pgo | tail -5
  for v in standard pgo; do strings "$HP/final-110-linux-$v/hydrothunder" | grep -m1 '1.1.0-beta' || true; done
fi
