#!/usr/bin/env bash
# Linux build of the current /root/ht/sdk (9.1.0 + B56/B57 patches), staged from the 9.1.0 AppImage,
# then a llvmpipe/Xvfb smoke run (software Vulkan: not an AMD hardware test).
set -uo pipefail
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
OLD="/mnt/e/PROJETOS ANTIGOS/Codex-arquivados-2026-10-21/2026-10-16/continue-o-projeto-hydro-thunder-hurricane/outputs"
GT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test
PKG=/root/ht/pkg/revision911-cand
T=/root/ht/pkg/smoke911
OUT="$HP/linux-llvmpipe-911"
if [ "${1:-}" != inside ]; then
  set -e
  export PATH="/root/ht/toolbin:/usr/bin:/bin"
  bash /root/ht/rebuild900-driver.sh 15 linux | tail -2
  rm -rf "$PKG" "$T"; mkdir -p "$PKG" "$OUT"
  (cd "$PKG"; "$OLD/HydroThunder-revisao-9.1.0.AppImage" --appimage-extract > extract.log)
  BIN="$PKG/squashfs-root/usr/bin"
  cp /root/ht/build-linux/hydrothunder "$BIN/"
  cp /root/ht/sdk/out/linux-amd64/*.so "$BIN/"
  for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do
    patchelf --set-rpath '$ORIGIN' "$BIN/$f"
  done
  (cd "$BIN"; ldd ./hydrothunder) > "$OUT/linux-dependencies.txt"
  ! grep -q 'not found' "$OUT/linux-dependencies.txt"
  mkdir -p "$T"; cp -a "$BIN/." "$T/"
  ln -s "$GT/game_data" "$T/game_data"
  cp -a "$GT/user_data" "$T/user_data"
  cp "$GT/hydrothunder.toml" "$T/hydrothunder.toml"
  sed -i 's/^ht_game_fullscreen = .*/ht_game_fullscreen = false/; s/^fullscreen = .*/fullscreen = false/' "$T/hydrothunder.toml"
  echo '0 0 0 0 0 0 0 0' > "$T/ht_test_input.txt"
  set +e
  exec xvfb-run -a -s '-screen 0 1280x720x24' bash "$0" inside
fi
cd "$T"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEO_DRIVER=x11
export HT_BOOT_TEST=1 HT_SETTINGS_ROOT="$T/settings"
export HT_TEST_CVARS='ht_dynamic_constants_verify=true;ht_float_constant_runs_verify=true;ht_texture_direct_verify=true;ht_direct_resolve_verify=true;ht_resolve_texture_verify=true;ht_request_range_fast_verify=true;ht_cp_fast_registers_verify=true;ht_async_submit_verify=true;ht_fetch_unchanged_verify=true'
mkdir -p "$T/settings"
./hydrothunder --ht_direct_launch=true > "$OUT/stdout.log" 2>&1 &
pid=$!
for s in 30 60 90 120 150 180 210; do
  sleep 30
  kill -0 "$pid" 2>/dev/null || break
  python3 -c "import os,sys;from PIL import ImageGrab;ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(sys.argv[1])" "$OUT/linux-$s.png" 2>/dev/null
done
kill -TERM "$pid" 2>/dev/null; sleep 3; kill -KILL "$pid" 2>/dev/null
L=$(ls -t "$T"/logs/*.log 2>/dev/null | head -1)
test -n "$L" || exit 1
cp "$L" "$OUT/game.log"
sha256sum ./hydrothunder ./*.so > "$OUT/tested-binaries.txt"
for t in ht-fconst ht-direct-verify ht-dres ht-rtt ht-dynamic-constants ht-cp-fast ht-async-submit ht-rrfast ht-b57-verify; do
  grep -o "\[$t[^]]*\].*" "$L" | tail -1 | cut -c1-170
done
grep -ciE 'VK_ERROR|crash|fatal' "$L"
