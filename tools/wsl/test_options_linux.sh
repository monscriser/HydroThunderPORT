#!/usr/bin/env bash
# 1.2.0 launcher options on Linux (Xvfb + llvmpipe), in the smoke-tested staging dir (its shader
# storage was filled by linux_120.sh): pre-compilation ON (progress screenshots), OFF, logs OFF.
set -uo pipefail
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
T=/root/ht/pkg/smoke120-pgo
OUT="$HP/linux-opcoes-120"
shot(){ python3 -c "import os,sys;from PIL import ImageGrab;ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])" "$1"; }
if [ "${1:-}" != inside ]; then rm -rf "$OUT"; mkdir -p "$OUT"; exec xvfb-run -a -s '-screen 0 1280x720x24' bash "$0" inside; fi
cd "$T"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11
run() {  # label seconds cvars...
  local L="$1" S="$2"; shift 2
  local H="$T/home-opt-$L"; rm -rf "$H"; mkdir -p "$H"
  rm -rf "$T/logs-before"; mkdir -p "$T/logs"; ls "$T/logs" > "$OUT/$L-logs-before.txt" 2>/dev/null
  HOME="$H" HT_BOOT_TEST=1 ./hydrothunder --ht_direct_launch=true "$@" > "$OUT/$L-stdout.log" 2>&1 & local pid=$!
  for s in 3 6 10 15; do sleep $(( s - ${prev:-0} )); prev=$s; shot "$OUT/$L-${s}s.png"; done; prev=0
  sleep $((S - 15)); kill -TERM $pid; sleep 2; kill -KILL $pid 2>/dev/null; wait $pid 2>/dev/null
  ls "$T/logs" > "$OUT/$L-logs-after.txt" 2>/dev/null
  echo "== $L: new log files: $(comm -13 "$OUT/$L-logs-before.txt" "$OUT/$L-logs-after.txt" | tr '\n' ' ')"
  local LOG=$(ls -t "$T"/logs/hydrothunder*.log 2>/dev/null | head -1)
  [ -n "$(comm -13 "$OUT/$L-logs-before.txt" "$OUT/$L-logs-after.txt")" ] && grep -a "ht-shaders" "$LOG" | cut -c1-200
}
run on 60
run off 45 --ht_shader_precompile=false
run nologs 45 --ht_write_logs=false
grep -a "ht-shaders\|ht_progress\|FATAL\|fatal" "$OUT"/*-stdout.log | head
