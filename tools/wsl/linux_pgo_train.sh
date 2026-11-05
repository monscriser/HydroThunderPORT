#!/usr/bin/env bash
# Linux PGO profile: instrumented binary (hydro-port/pgo-GEN-linux) races under Xvfb + llvmpipe with
# the virtual pad (HT_BOOT_TEST). usage: linux_pgo_train.sh asgard|outra <name>
# The save picks the track (Ian's save = Asgard). Works in a copy: /root/ht/pkg/pgo-linux-run.
set -uo pipefail
TRACK="$1"; NAME="$2"
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
GT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test
T=/root/ht/pkg/pgo-linux-run
if [ "${3:-}" != inside ]; then
  set -e
  rm -rf "$T"; cp -a /root/ht/pkg/smoke920-final "$T"
  cp "$HP/pgo-GEN-linux/hydrothunder" "$HP/pgo-GEN-linux/"*.so "$T/"
  for f in hydrothunder librexruntime.so librexgpu-xenos.so; do patchelf --set-rpath '$ORIGIN' "$T/$f"; done
  SAVE="$T/user_data/B13EBABEBABEBABE/5841096A/00000001/ht2save.dat/ht2save.dat"
  if [ "$TRACK" = outra ]; then
    cp "/mnt/e/hydrothunder/HYDRA JOGO/save_backups_claude/atual-antes-de-restaurar-0824/00000001/ht2save.dat/ht2save.dat" "$SAVE"
  else
    cp "/mnt/e/hydrothunder/HYDRA JOGO/save_backups_claude/B13EBABEBABEBABE/5841096A/00000001/ht2save.dat/ht2save.dat" "$SAVE"
  fi
  mkdir -p "$HP/pgo-prof-linux"; rm -f "$T"/logs/*.log
  set +e
  exec xvfb-run -a -s '-screen 0 1280x720x24' bash "$0" "$TRACK" "$NAME" inside
fi
cd "$T"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11
export HT_BOOT_TEST=1 HT_SETTINGS_ROOT="$T/settings-pgo" LLVM_PROFILE_FILE="$HP/pgo-prof-linux/$NAME.profraw"
mkdir -p "$HT_SETTINGS_ROOT"
pad(){ echo "$1 $2 0 0 $3 0 0 0" > "$T/ht_test_input.txt"; }
pad 0 0 0
./hydrothunder --ht_direct_launch=true > /root/ht/pkg/pgo-linux-$NAME.stdout 2>&1 & pid=$!
sleep 25
# Menus: A presses (Start once early), like gpu_race_batch.ps1.
for i in $(seq 0 70); do
  if [ $i -eq 3 ]; then pad 16 0 0; else pad 4096 0 0; fi; sleep 0.15; pad 0 0 0; sleep 1.6
  kill -0 $pid 2>/dev/null || break
done
# Race: throttle with steering taps.
for t in $(seq 0 300); do
  case $((t % 6)) in 1) lx=-8000;; 4) lx=8000;; *) lx=0;; esac
  pad 0 255 $lx; sleep 0.4
  kill -0 $pid 2>/dev/null || break
done
sleep 12   # one more profile write
python3 -c "import os,sys;from PIL import ImageGrab;ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])" "$HP/pgo-prof-linux/$NAME.png" 2>/dev/null
kill -TERM $pid; sleep 3; kill -KILL $pid 2>/dev/null
L=$(ls -t "$T"/logs/*.log | head -1)
grep -oE '\[ht-perf\] t=[0-9]+s fps=[0-9.]+.*draws=[0-9]+' "$L" | sed -E 's/max_frame.*draws=/draws=/' | awk 'NR%8==0' | tail -8
ls -la "$HP/pgo-prof-linux/$NAME.profraw"
