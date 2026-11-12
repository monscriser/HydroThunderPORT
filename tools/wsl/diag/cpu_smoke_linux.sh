#!/usr/bin/env bash
# Runs the CURRENT build-linux binaries under Xvfb/llvmpipe for N seconds (player-style launch,
# fresh HOME) and reports per-thread CPU (ps, averaged over the last 60 s) plus selected log tags.
# usage: cpu_smoke_linux.sh <label> [seconds=150] [log grep regex] [extra env K=V ...]
set -uo pipefail
L="$1"; SECS="${2:-150}"; RX="${3:-ht-wm|ht-audio|ht-bind}"; shift 3 2>/dev/null || shift $#
GT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test
W=/root/ht/pkg/cpusmoke-$L
OUT="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port/cpu-smoke/$L"
if [ "${INSIDE:-}" != 1 ]; then
  rm -rf "$W"; mkdir -p "$W/run" "$W/home" "$OUT"
  cp -a /root/ht/pkg/revision110-pgo/squashfs-root/usr/bin/. "$W/run/"
  cp /root/ht/build-linux/hydrothunder /root/ht/sdk/out/linux-amd64/librexruntime.so /root/ht/sdk/out/linux-amd64/librexgpu-xenos.so "$W/run/"
  for f in hydrothunder librexruntime.so librexgpu-xenos.so; do /root/ht/toolbin/patchelf --set-rpath '$ORIGIN' "$W/run/$f" 2>/dev/null || patchelf --set-rpath '$ORIGIN' "$W/run/$f"; done
  ln -s "$GT/game_data" "$W/run/game_data"; cp -a "$GT/user_data" "$W/run/user_data"
  cp "$GT/hydrothunder.toml" "$W/run/hydrothunder.toml"
  sed -i 's/^ht_game_fullscreen = .*/ht_game_fullscreen = false/; s/^fullscreen = .*/fullscreen = false/' "$W/run/hydrothunder.toml"
  INSIDE=1 exec xvfb-run -a -s '-screen 0 1280x720x24' env "$@" INSIDE=1 bash "$0" "$L" "$SECS" "$RX"
fi
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11
cd "$W/run"
HOME="$W/home" ./hydrothunder --ht_direct_launch=true > "$W/stdout.log" 2>&1 &
pid=$!
sleep $((SECS - 60))
# per-thread CPU ticks over the last 60 s
snap() { for t in /proc/$pid/task/*; do echo "$(basename $t) $(cat $t/comm 2>/dev/null | tr ' ' '_') $(sed 's/.*) //' $t/stat 2>/dev/null | awk '{print $12+$13}')"; done; }
snap > "$W/a.txt"; sleep 60; snap > "$W/b.txt"
kill -TERM $pid; sleep 3; kill -KILL $pid 2>/dev/null; wait $pid 2>/dev/null
HZ=$(getconf CLK_TCK)
join <(sort "$W/a.txt") <(sort "$W/b.txt") | awk -v hz=$HZ '{d=$5-$3; if (d>0) printf "%6.1f%% %s#%s\n", 100*d/hz/60, $2, $1}' | sort -rn | head -12 | tee "$OUT/threads-last60s.txt"
LOG=$(ls -t "$W/home/.local/share/HydroThunderRecompiled/logs/"hydrothunder*.log "$W/run/logs/"hydrothunder*.log 2>/dev/null | head -1)
cp "$LOG" "$OUT/game.log"
grep -aE "$RX" "$LOG" | tail -8 | cut -c1-260
grep -aciE 'VK_ERROR|fatal' "$LOG" || true
