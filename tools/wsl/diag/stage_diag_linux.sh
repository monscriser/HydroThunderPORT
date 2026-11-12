#!/usr/bin/env bash
# Stage the diagnostic AppImage dir from the 1.1.0 PGO AppDir + diag binaries, then:
#  A) RenderDoc proof: a FAKE librenderdoc.so (only writes a marker file when loaded)
#     on LD_LIBRARY_PATH - like SteamOS's system-wide /usr/lib/librenderdoc.so.
#     1.1.0 PGO (old) must load it; the diag build (fixed) must not.
#  B) llvmpipe/Xvfb smoke of the diag build as a normal player launch (fresh HOME),
#     checking that the telemetry session folder is written.
# usage: stage_diag_linux.sh [seconds for B, default 240]
set -uo pipefail
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
BIN_DIAG="$HP/diag-110-linux"
GT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
PKG=/root/ht/pkg/revision110-diag
W=/root/ht/pkg/diagtest
OUT="$HP/linux-llvmpipe-110-diag"
SECS="${1:-240}"
if [ "${1:-}" != inside ]; then
  set -e
  export PATH="/root/ht/toolbin:/usr/bin:/bin"
  rm -rf "$PKG" "$W"; mkdir -p "$PKG" "$W" "$OUT"
  cp -a /root/ht/pkg/revision110-pgo/squashfs-root "$PKG/"
  BIN="$PKG/squashfs-root/usr/bin"
  cp "$BIN_DIAG/"* "$BIN/"
  for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do patchelf --set-rpath '$ORIGIN' "$BIN/$f"; done
  rm -f "$BIN/NOTAS-1.1.0-BETA.md"
  (cd "$BIN"; ldd ./hydrothunder) > "$OUT/linux-dependencies.txt"
  ! grep -q 'not found' "$OUT/linux-dependencies.txt"
  # Fake RenderDoc.
  mkdir -p "$W/fakerd"
  cat > "$W/fakerd/rd.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
__attribute__((constructor)) static void ht_fake_rd_loaded(void) {
  const char* f = getenv("FAKE_RD_MARK");
  if (f) { FILE* o = fopen(f, "w"); if (o) { fputs("librenderdoc.so was LOADED into the game\n", o); fclose(o); } }
}
int RENDERDOC_GetAPI(int version, void** out) { (void)version; if (out) *out = 0; return 0; }
C
  cc -shared -fPIC -o "$W/fakerd/librenderdoc.so" "$W/fakerd/rd.c"
  for v in old diag; do
    D="$W/run-$v"; mkdir -p "$D"
    if [ $v = old ]; then cp -a /root/ht/pkg/revision110-pgo/squashfs-root/usr/bin/. "$D/"; else cp -a "$BIN/." "$D/"; fi
    ln -s "$GT/game_data" "$D/game_data"; cp -a "$GT/user_data" "$D/user_data"
    cp "$GT/hydrothunder.toml" "$D/hydrothunder.toml"
    sed -i 's/^ht_game_fullscreen = .*/ht_game_fullscreen = false/; s/^fullscreen = .*/fullscreen = false/' "$D/hydrothunder.toml"
    echo '0 0 0 0 0 0 0 0' > "$D/ht_test_input.txt"
  done
  set +e
  exec xvfb-run -a -s '-screen 0 1280x720x24' bash "$0" inside "$SECS"
fi
SECS="$2"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11
export LD_LIBRARY_PATH="$W/fakerd"
run() {  # dir seconds home
  (cd "$1"; HOME="$3" FAKE_RD_MARK="$1/RENDERDOC-LOADED.txt" ./hydrothunder --ht_direct_launch=true > "$1/stdout.log" 2>&1) &
  local pid=$!
  local s=0
  while [ $s -lt "$2" ] && kill -0 $pid 2>/dev/null; do sleep 5; s=$((s+5)); done
  python3 -c "import os,sys;from PIL import ImageGrab;ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])" "$OUT/$(basename "$1").png" 2>/dev/null
  pkill -TERM -f "^./hydrothunder --ht_direct_launch" ; sleep 3; pkill -KILL -f "^./hydrothunder --ht_direct_launch"
  wait $pid 2>/dev/null
}
for v in old diag; do
  mkdir -p "$W/home-$v"
  if [ $v = old ]; then run "$W/run-$v" 75 "$W/home-$v"; else run "$W/run-$v" "$SECS" "$W/home-$v"; fi
  if [ -f "$W/run-$v/RENDERDOC-LOADED.txt" ]; then echo "[$v] fake librenderdoc.so: LOADED (overlay bug)"; else echo "[$v] fake librenderdoc.so: not loaded"; fi
  grep -ah "RenderDoc\|debug markers auto" "$W/home-$v/.local/share/HydroThunderRecompiled/logs/"*.log 2>/dev/null | head -3
done
DS=$(ls -d "$W/home-diag/.local/share/HydroThunderRecompiled/diag/"* 2>/dev/null | head -1)
echo "session: $DS"
rm -rf "$OUT/session"; [ -n "$DS" ] && cp -a "$DS" "$OUT/session"
cp -a "$W/home-diag/.local/share/HydroThunderRecompiled/logs" "$OUT/logs-diag" 2>/dev/null
cp -a "$W/home-old/.local/share/HydroThunderRecompiled/logs" "$OUT/logs-old" 2>/dev/null
cp "$W"/run-*/RENDERDOC-LOADED.txt "$OUT/" 2>/dev/null
[ -n "$DS" ] && { wc -l "$DS"/*.csv; ls -la "$DS"; grep -a "ht-track\|ht-diag" "$DS/game.log" | head; grep -ac "\[ht-perf\] t=" "$DS/game.log"; grep -a "ht-vram\|ht-pipe" "$DS/game.log" | tail -2; grep -ai "\.fsb\|\.apf" "$DS/opens.csv" | head -20; }
grep -aciE "VK_ERROR|crash|fatal" "$W/home-diag/.local/share/HydroThunderRecompiled/logs/"hydro*.log || true
