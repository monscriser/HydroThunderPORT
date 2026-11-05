#!/usr/bin/env bash
# Linux A/B under Xvfb + llvmpipe (virtual pad race, like linux_pgo_train.sh). llvmpipe makes the
# software renderer the FPS limit, so the comparison is CPU of the GAME threads per frame: per-thread
# CPU (/proc) over a 60 s race window divided by the frames the game counted ([ht-perf]).
# usage: linux_ab.sh <bin folder in hydro-port> asgard|outra <name>
set -uo pipefail
BIN="$1"; TRACK="$2"; NAME="$3"
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
T=/root/ht/pkg/linux-ab-run
OUT="$HP/linux-ab/$NAME"
if [ "${4:-}" != inside ]; then
  set -e
  rm -rf "$T"; cp -a /root/ht/pkg/smoke920-final "$T"
  cp "$HP/$BIN/hydrothunder" "$HP/$BIN/"*.so "$T/"
  for f in hydrothunder librexruntime.so librexgpu-xenos.so; do patchelf --set-rpath '$ORIGIN' "$T/$f"; done
  SAVE="$T/user_data/B13EBABEBABEBABE/5841096A/00000001/ht2save.dat/ht2save.dat"
  if [ "$TRACK" = outra ]; then S="/mnt/e/hydrothunder/HYDRA JOGO/save_backups_claude/atual-antes-de-restaurar-0824/00000001/ht2save.dat/ht2save.dat"
  else S="/mnt/e/hydrothunder/HYDRA JOGO/save_backups_claude/B13EBABEBABEBABE/5841096A/00000001/ht2save.dat/ht2save.dat"; fi
  cp "$S" "$SAVE"
  rm -rf "$OUT"; mkdir -p "$OUT"; rm -f "$T"/logs/*.log
  set +e
  exec xvfb-run -a -s '-screen 0 1280x720x24' bash "$0" "$BIN" "$TRACK" "$NAME" inside
fi
cd "$T"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11
export HT_BOOT_TEST=1 HT_SETTINGS_ROOT="$T/settings-ab" HT_TEST_CVARS='ht_perf_log=true'
mkdir -p "$HT_SETTINGS_ROOT"
pad(){ echo "$1 $2 0 0 $3 0 0 0" > "$T/ht_test_input.txt"; }
snap(){ for t in /proc/$pid/task/*; do c=$(cat $t/comm 2>/dev/null); s=($(sed 's/^.*) //' $t/stat 2>/dev/null)); echo "${t##*/} $((s[11]+s[12])) $c"; done; }
pad 0 0 0
./hydrothunder --ht_direct_launch=true > "$OUT/stdout.log" 2>&1 & pid=$!
sleep 25
for i in $(seq 0 70); do
  if [ $i -eq 3 ]; then pad 16 0 0; else pad 4096 0 0; fi; sleep 0.15; pad 0 0 0; sleep 1.6
done
t=0; lx(){ case $((t % 6)) in 1) echo -8000;; 4) echo 8000;; *) echo 0;; esac; }
for t in $(seq 0 50); do pad 0 255 $(lx); sleep 0.4; done          # 20 s into the race
date +%H:%M:%S > "$OUT/window-start.txt"; snap > "$OUT/cpu-start.txt"
for t in $(seq 51 200); do pad 0 255 $(lx); sleep 0.4; done        # 60 s window
snap > "$OUT/cpu-end.txt"; date +%H:%M:%S > "$OUT/window-end.txt"
kill -TERM $pid; sleep 3; kill -KILL $pid 2>/dev/null
cp "$(ls -t "$T"/logs/*.log | head -1)" "$OUT/game.log"
python3 - "$OUT" <<'EOF'
import sys, re, os
out = sys.argv[1]
def load(f):
    d = {}
    for l in open(os.path.join(out, f)):
        p = l.split(None, 2)
        if len(p) >= 2: d[p[0]] = (int(p[1]), p[2].strip() if len(p) > 2 else '')
    return d
a, b = load('cpu-start.txt'), load('cpu-end.txt')
s, e = open(os.path.join(out, 'window-start.txt')).read().strip(), open(os.path.join(out, 'window-end.txt')).read().strip()
frames = 0.0
for l in open(os.path.join(out, 'game.log'), errors='replace'):
    m = re.search(r'(\d\d:\d\d:\d\d)\.\d+\].*\[ht-perf\] t=\d+s fps=([\d.]+)', l)
    if m and s <= m.group(1) <= e: frames += float(m.group(2)) * 2   # one line every 2 s
tick = os.sysconf('SC_CLK_TCK')
rows = sorted(((b[t][0] - a[t][0]) * 1000 / tick, b[t][1]) for t in b if t in a)[::-1]
game = sum(ms for ms, n in rows if not n.startswith('llvmpipe'))
print(f'frames={frames:.0f} | game threads (no llvmpipe) {game/max(frames,1):.3f} ms/frame | top:',
      ', '.join(f'{n}={ms/max(frames,1):.3f}' for ms, n in rows[:5]))
EOF
