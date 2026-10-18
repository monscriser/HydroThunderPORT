#!/usr/bin/env bash
# Offscreen software Vulkan; this is not an AMD hardware performance test.
set -uo pipefail
TASK=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane
OUT="$TASK/outputs/validacao-8.9.3/linux-llvmpipe"
T=/root/ht/pkg/smoke893-final
mkdir -p "$OUT"
if [ "${1:-}" != inside ]; then
  exec xvfb-run -a -s '-screen 0 1280x720x24' bash "$0" inside
fi
cd "$T"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11
export HT_BOOT_TEST=1 HT_SETTINGS_ROOT="$T/settings"
export HT_TEST_CVARS='ht_zpd_batch=true;ht_dynamic_constants=true;ht_dynamic_constants_verify=true;ht_float_constant_runs_verify=true;ht_texture_direct_verify=true;ht_direct_resolve_verify=true;ht_resolve_texture_verify=true;ht_request_range_fast_verify=true'
mkdir -p "$T/settings"
./hydrothunder --ht_direct_launch=true > "$OUT/stdout.log" 2>&1 &
pid=$!
for s in 30 60 90 120; do
  sleep 30
  kill -0 "$pid" 2>/dev/null || break
  python3 -c "import os,sys;from PIL import ImageGrab;ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])" "$OUT/linux-$s.png" 2>/dev/null
done
kill -TERM "$pid" 2>/dev/null; sleep 3; kill -KILL "$pid" 2>/dev/null
L=$(ls -t "$T"/logs/*.log 2>/dev/null | head -1)
test -n "$L" || exit 1
cp "$L" "$OUT/game.log"
sha256sum ./hydrothunder ./*.so > "$OUT/tested-binaries.txt"
grep -E 'ht-fconst|ht-rtt|ht-direct|ht-dres|ht-dynamic-constants|VK_ERROR|crash|fatal' -i "$L" | tail -10
