#!/bin/bash
# Linux smoke test of the GPU optimizations on llvmpipe (no real GPU): runs the
# new build with the isolated test game data (symlinked read-only game_data,
# copied user_data), proof cvars on, then reports the proof lines and a capture.
# usage: smoke_cpu87.sh [seconds]
set -uo pipefail
BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
GT="$BASE/outputs/_teste_modern/game-test"
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/outputs/provas-cpu/linux-llvmpipe-87
T=/root/ht/pkg/smoke87-20261016-1010
SECS="${1:-120}"
if [ "${2:-}" != inside ]; then
  [ ! -e "$T" ] || { echo "Smoke staging already exists; preserve it and choose another T"; exit 1; }; mkdir -p "$T" "$OUT"
  cp -a /root/ht/pkg/Hydro-Thunder-Linux-revisao-7-portatil/. "$T/"
  # The SDK libraries are built in sdk/out/linux-amd64 (build-linux only gets a copy when the exe relinks).
  cp /root/ht/build-linux/hydrothunder "$T/hydrothunder"
  for f in librexruntime.so librexgpu-xenos.so; do cp /root/ht/sdk/out/linux-amd64/$f "$T/$f"; done
  for f in hydrothunder librexruntime.so librexgpu-xenos.so; do patchelf --set-rpath '$ORIGIN' "$T/$f"; done
  ln -s "$GT/game_data" "$T/game_data"
  cp -a "$GT/user_data" "$T/user_data"
  [ -d "$GT/content_ui" ] && cp -a "$GT/content_ui" "$T/content_ui"
  cp "$GT/hydrothunder.toml" "$T/hydrothunder.toml"
  sed -i 's/^ht_game_fullscreen = .*/ht_game_fullscreen = false/; s/^fullscreen = .*/fullscreen = false/' "$T/hydrothunder.toml"
  echo '0 0 0 0 0 0 0 0' > "$T/ht_test_input.txt"
  exec xvfb-run -a -s '-screen 0 1280x720x24' bash "$0" "$SECS" inside
fi
cd "$T"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11
export HT_BOOT_TEST=1 HT_SETTINGS_ROOT="$T/settings" HT_TEST_CVARS="${SMOKE_CVARS:-ht_texture_direct_verify=true;ht_resolve_texture_verify=true}"
mkdir -p "$T/settings"
./hydrothunder --ht_direct_launch=true > "$OUT/stdout.log" 2>&1 &
pid=$!
for s in 30 60 90 "$SECS"; do
  sleep $(( s - ${prev:-0} )); prev=$s
  kill -0 $pid 2>/dev/null || { echo "processo terminou cedo"; break; }
  python3 -c "import os,sys;from PIL import ImageGrab;ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])" "$OUT/linux-$s.png" 2>/dev/null
done
kill -TERM $pid 2>/dev/null; sleep 4; kill -KILL $pid 2>/dev/null
L=$(ls -t "$T"/logs/*.log ~/.local/share/HydroThunderRecompiled/logs/*.log 2>/dev/null | head -1)
cp "$L" "$OUT/game.log" 2>/dev/null
echo "log=$L"
grep -E 'ht-rtt|ht-direct|ht-dres|ht-cutout|deviceName|llvmpipe|VK_ERROR|crash|fatal' -i "$L" | tail -12
