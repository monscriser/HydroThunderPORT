#!/usr/bin/env bash
# 1.2.0 launcher on Linux (Xvfb + llvmpipe), from the smoke-tested staging dir (smoke120-pgo):
# InÃ­cio and Novidades at 1280x800 (Ally/Deck screen) and 900x700. Screenshots only.
set -uo pipefail
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
T=/root/ht/pkg/smokefinal-pgo
OUT="$HP/linux-launcher-final"
shot(){ python3 -c "import os,sys;from PIL import ImageGrab;ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])" "$1"; }
if [ "${1:-}" != inside ]; then
  rm -rf "$OUT"; mkdir -p "$OUT"
  for size in 1280x800 900x700; do
    xvfb-run -a -s "-screen 0 ${size}x24" bash "$0" inside "$size"
  done
  for f in "$OUT"/*.log; do echo "$(basename "$f"): imgui-error=$(grep -c imgui-error "$f") lines=$(wc -l < "$f")"; done; exit 0
fi
SIZE="$2"; W=${SIZE%x*}; H=${SIZE#*x}
S="$T/settings-launcher-$SIZE"; rm -rf "$S"; mkdir -p "$S"
printf 'HTH-LIBRARY-1\n"%s"\n' "$T" > "$S/library-location.txt"
cd "$T"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11 HT_SETTINGS_ROOT="$S"
for tab in 0 5 1; do
  HT_LAUNCHER_TAB=$tab ./hydrothunder --ht_import_on_start=true --ht_write_logs=true --window_width=$W --window_height=$H > "$OUT/launcher-$SIZE-tab$tab.log" 2>&1 & pid=$!
  sleep 25; shot "$OUT/linux-$SIZE-tab$tab.png"; kill -TERM $pid; sleep 2; kill -KILL $pid 2>/dev/null; wait $pid 2>/dev/null
done
