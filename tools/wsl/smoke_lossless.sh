#!/bin/bash
set -uo pipefail
BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
OUT="$BASE/outputs/_teste_lsfg"
LAYOUT=$(cut -d= -f2- "$OUT/linux-layout.txt" | tr -d '\r')
cd "$LAYOUT/usr/bin" || exit 1
export HT_SETTINGS_ROOT="$OUT/linux-smoke-settings"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export SDL_VIDEO_DRIVER=x11
timeout 15 xvfb-run -a -s '-screen 0 1280x720x24' ./hydrothunder --ht_import_on_start=true > "$OUT/linux-smoke.log" 2>&1
code=$?
echo "exit=$code (124 means stopped by the test timeout)" >> "$OUT/linux-smoke.log"
ldd ./hydrothunder > "$OUT/linux-dependencies.log"
tail -12 "$OUT/linux-smoke.log"
test "$code" = 124
