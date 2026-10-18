#!/usr/bin/env bash
set -euo pipefail
TASK=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane
OLD=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/outputs
PKG=/root/ht/pkg/revision893-final
mkdir -p "$PKG"
test ! -e "$PKG/squashfs-root"
(cd "$PKG"; "$OLD/HydroThunder-revisao-8.9.2-preview.AppImage" --appimage-extract > extract.log)
BIN="$PKG/squashfs-root/usr/bin"
cp /root/ht/build-linux/hydrothunder "$BIN/"
cp /root/ht/sdk/out/linux-amd64/*.so "$BIN/"
for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do
  patchelf --set-rpath '$ORIGIN' "$BIN/$f"
done
sha256sum "$BIN/hydrothunder" "$BIN/"*.so > "$TASK/outputs/validacao-8.9.3/build/linux-normalized-hashes.txt"
(cd "$BIN"; ldd ./hydrothunder) > "$TASK/outputs/validacao-8.9.3/build/linux-dependencies.txt"
! grep -q 'not found' "$TASK/outputs/validacao-8.9.3/build/linux-dependencies.txt"
T=/root/ht/pkg/smoke893-final
GT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test
test ! -e "$T"
mkdir -p "$T"
cp -a "$BIN/." "$T/"
ln -s "$GT/game_data" "$T/game_data"
cp -a "$GT/user_data" "$T/user_data"
if [ -d "$GT/content_ui" ]; then cp -a "$GT/content_ui" "$T/content_ui"; fi
cp "$GT/hydrothunder.toml" "$T/hydrothunder.toml"
sed -i 's/^ht_game_fullscreen = .*/ht_game_fullscreen = false/; s/^fullscreen = .*/fullscreen = false/' "$T/hydrothunder.toml"
echo '0 0 0 0 0 0 0 0' > "$T/ht_test_input.txt"
echo 'Linux final staging ready, before smoke test.'
