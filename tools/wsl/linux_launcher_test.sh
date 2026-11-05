#!/usr/bin/env bash
# Linux (Xvfb + llvmpipe): new launcher screenshot, user_data folder change, game run with the
# chosen folder. Works in /root/ht/pkg/smoke911 (staged earlier from the 9.1.0 AppImage).
set -uo pipefail
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
T=/root/ht/pkg/smoke911
OUT="$HP/linux-launcher"
S="$T/settings-launcher"
UD=/root/ht/pkg/ud-linux-escolhida
shot(){ python3 -c "import os,sys;from PIL import ImageGrab;ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])" "$1"; }
if [ "${1:-}" != inside ]; then
  set -e
  cp /root/ht/build-linux/hydrothunder "$T/"; cp /root/ht/sdk/out/linux-amd64/*.so "$T/"
  for f in hydrothunder librexruntime.so librexgpu-xenos.so; do patchelf --set-rpath '$ORIGIN' "$T/$f"; done
  cp "$SRC/ui/Roboto-Medium.ttf" "$T/ui/"
  rm -rf "$S" "$UD" "$OUT"; mkdir -p "$S" "$OUT"
  printf 'HTH-LIBRARY-1\n"%s"\n' "$T" > "$S/library-location.txt"
  set +e
  exec xvfb-run -a -s '-screen 0 1000x760x24' bash "$0" inside
fi
cd "$T"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11 HT_SETTINGS_ROOT="$S"
# 1) launcher, Home tab
./hydrothunder --ht_import_on_start=true > "$OUT/launcher.log" 2>&1 & pid=$!
sleep 20; shot "$OUT/linux-inicio.png"; kill -TERM $pid; sleep 2; kill -KILL $pid 2>/dev/null
# 2) pick a folder for user_data (test hook), Saves tab
HT_TEST_PICK_USER_DATA="$UD" ./hydrothunder --ht_import_on_start=true > "$OUT/pick.log" 2>&1 & pid=$!
sleep 25; shot "$OUT/linux-saves.png"; kill -TERM $pid; sleep 2; kill -KILL $pid 2>/dev/null
echo "location: $(tr '\n' ' ' < "$S/user-data-location.txt")"
echo "files old=$(find "$T/user_data" -type f | wc -l) new=$(find "$UD/user_data" -type f 2>/dev/null | wc -l)"
# 3) the game with the chosen folder (virtual pad mode); it rewrites the DLC header in the folder it uses
before=$(date +%s)
HT_BOOT_TEST=1 ./hydrothunder --ht_direct_launch=true > "$OUT/game.log" 2>&1 & pid=$!
sleep 60; shot "$OUT/linux-jogo.png"; kill -TERM $pid; sleep 3; kill -KILL $pid 2>/dev/null
for d in "$T/user_data" "$UD/user_data"; do
  h=$(find "$d" -name 'Pack01.header' | head -1); echo "$d header_written_after_start=$([ $(stat -c %Y "$h") -ge $before ] && echo yes || echo no)"
done
L=$(ls -t "$T"/logs/*.log 2>/dev/null | head -1); grep -o '\[ht-content\].*' "$L" | head -2
