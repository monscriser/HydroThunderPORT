#!/usr/bin/env bash
set -euo pipefail

BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT="$BASE/outputs"
WIN="$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.5-preview/data"

cp /root/ht/build-win/hydrothunder.exe "$WIN/hydrothunder.exe"
cp /root/ht/sdk/out/win-amd64/librexruntime.dll "$WIN/librexruntime.dll"
# The SDK DLLs are built in sdk/out, not in build-win.
cp /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll "$WIN/rexgpu-xenos.dll"

PKG="/root/ht/pkg/revision-8.5-preview-$(date +%s)"
mkdir -p "$PKG"
cp "$OUT/HydroThunder-revisao-7.AppImage" "$PKG/base.AppImage"
chmod +x "$PKG/base.AppImage"
(
  cd "$PKG"
  ./base.AppImage --appimage-extract > extract.log
)
BIN="$PKG/squashfs-root/usr/bin"
# The SDK libraries are built in sdk/out/linux-amd64 (build-linux only has a copy from the last exe relink).
cp /root/ht/build-linux/hydrothunder "$BIN/hydrothunder"
for file in librexruntime.so librexgpu-xenos.so; do
  cp "/root/ht/sdk/out/linux-amd64/$file" "$BIN/$file"
done
for file in hydrothunder librexruntime.so librexgpu-xenos.so; do
  patchelf --set-rpath '$ORIGIN' "$BIN/$file"
done
cp "$OUT/NOTAS-revisao-8.5-preview.md" "$OUT/RESULTADOS-GPU-revisao-8.5.md" "$SRC/reports/Reduzir uso de GPU no Xenia.md" "$BIN/"
cp "$BASE/work/appimagetool-x86_64.AppImage" "$PKG/appimagetool"
chmod +x "$PKG/appimagetool"
ARCH=x86_64 "$PKG/appimagetool" --appimage-extract-and-run "$PKG/squashfs-root" "$OUT/HydroThunder-revisao-8.5-preview.AppImage" > "$OUT/_teste_modern/package-linux-8.5.log" 2>&1
(
  cd "$BIN"
  ldd ./hydrothunder
) > "$OUT/_teste_modern/linux-dependencies-8.5.log"
if grep -q 'not found' "$OUT/_teste_modern/linux-dependencies-8.5.log"; then
  echo 'Missing dependencies' >&2
  exit 1
fi
echo "AppDir=$PKG/squashfs-root" > "$OUT/_teste_modern/linux-layout-8.5.txt"
python3 "$SRC/tools/make_zip.py" "$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.5-preview" "$OUT/Hydro-Thunder-Windows-revisao-8.5-preview.zip"
ls -lh "$OUT/HydroThunder-revisao-8.5-preview.AppImage" "$OUT/Hydro-Thunder-Windows-revisao-8.5-preview.zip"
