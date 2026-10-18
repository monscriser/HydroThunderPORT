#!/bin/bash
# Builds the rev7 Linux AppImage + portable tarball from the rev6 AppImage layout.
# usage: package_linux.sh <source snapshot dir (WSL path)> <rev6 AppImage> <output dir>
set -euo pipefail
SRC="$1"; REV6="$2"; OUT="$3"
HT=/root/ht
P="$HT/pkg"; A="$P/AppDir"
mkdir -p "$P" "$OUT"
rm -rf "$A" "$P/r6"; mkdir -p "$P/r6"
cp "$REV6" "$P/r6/r6.AppImage"; chmod +x "$P/r6/r6.AppImage"
(cd "$P/r6" && ./r6.AppImage --appimage-extract >/dev/null)
mv "$P/r6/squashfs-root" "$A"
BIN="$A/usr/bin"
cp "$HT/build-linux/hydrothunder" "$HT/build-linux/ht-import" "$BIN/"
cp "$HT/build-linux/librexruntime.so" "$HT/build-linux/librexgpu-xenos.so" "$BIN/"
cp "$HT/sdk/out/linux-amd64/libamd_fidelityfx_vk.so" "$BIN/"
for f in hydrothunder ht-import librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do
  patchelf --set-rpath '$ORIGIN' "$BIN/$f"
done
python3 "$SRC/tools/pkg_toml.py" "$BIN/hydrothunder.toml"
cp "$SRC/LEIA-ME-revisao-7.md" "$BIN/LEIA-ME.md"
rm -rf "$BIN/translations" && cp -r "$SRC/translations" "$BIN/translations"
rm -rf "$BIN/steam_art" && mkdir -p "$BIN/steam_art" && cp -r "$SRC/steam_art/." "$BIN/steam_art/" && rm -f "$BIN/steam_art/previa_steam.jpg"
cp "$HT/build-linux/_deps/fidelityfx-src/LICENSE.txt" "$BIN/licenses/AMD-FidelityFX-SDK-LICENSE.txt"
echo "== deps"
for f in hydrothunder ht-import librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do
  missing=$(cd "$BIN" && ldd "./$f" | grep 'not found' || true)
  [ -z "$missing" ] || { echo "MISSING in $f: $missing"; exit 1; }
  echo "$f: $(readelf -d "$BIN/$f" | grep -oE 'RUNPATH.*')"
done
echo "== appimage"
APPIMAGETOOL="$SRC/../appimagetool-x86_64.AppImage"
cp "$APPIMAGETOOL" "$P/appimagetool"; chmod +x "$P/appimagetool"
rm -f "$OUT/HydroThunder-revisao-7.AppImage"
ARCH=x86_64 "$P/appimagetool" --appimage-extract-and-run "$A" "$OUT/HydroThunder-revisao-7.AppImage" > "$P/appimagetool.log" 2>&1 \
  || { tail -20 "$P/appimagetool.log"; exit 1; }
echo "== portable"
PORT="$P/Hydro-Thunder-Linux-revisao-7-portatil"
tr -d '\r' < "$SRC/tools/portable_files.sh" > "$P/portable_files.sh"
sh "$P/portable_files.sh" "$A" "$PORT"
(cd "$P" && tar czf "$OUT/Hydro-Thunder-Linux-revisao-7-portatil.tar.gz" "$(basename "$PORT")")
ls -la "$OUT"
