#!/bin/bash
set -euo pipefail
BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
OUT="$BASE/outputs/_teste_modern"
if [ "${1:-}" != inside ]; then
  exec xvfb-run -a -s '-screen 0 1100x850x24' bash "$0" inside
fi
LAYOUT=$(cut -d= -f2- "$OUT/linux-layout.txt" | tr -d '\r')
cd "$LAYOUT/usr/bin"
export HT_SETTINGS_ROOT="$OUT/linux-fresh-settings"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export SDL_VIDEO_DRIVER=x11
./hydrothunder --ht_import_on_start=true > "$OUT/linux-configurator.log" 2>&1 &
test_pid=$!
trap 'kill -TERM "$test_pid" 2>/dev/null || true' EXIT
sleep 6
kill -0 "$test_pid"
python3 - "$OUT/launcher-linux-first-run.png" <<'PY'
import os,sys
from PIL import ImageGrab
ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])
print('Linux virtual-display capture saved')
PY
kill -TERM "$test_pid"
wait "$test_pid" || true
trap - EXIT
