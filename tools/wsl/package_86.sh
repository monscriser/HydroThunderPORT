#!/usr/bin/env bash
set -euo pipefail

BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT="$BASE/outputs"
WIN="$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.6-preview/data"
# Start from the 8.5 stage: only the three binaries change.
if [ ! -d "$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.6-preview" ]; then
  cp -a "$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.5-preview" \
    "$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.6-preview"
fi
# Release notes: 8.6 in, the inherited 8.5 file out (moved, never deleted).
if [ -f "$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.6-preview/NOTAS-revisao-8.5-preview.md" ]; then
  mkdir -p "$OUT/_teste_modern/_to_delete"
  mv "$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.6-preview/NOTAS-revisao-8.5-preview.md" \
    "$OUT/_teste_modern/_to_delete/NOTAS-revisao-8.5-preview.from-8.6-stage.md"
fi
cp "$OUT/NOTAS-revisao-8.6-preview.md" "$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.6-preview/"

cp /root/ht/build-win/hydrothunder.exe "$WIN/hydrothunder.exe"
cp /root/ht/sdk/out/win-amd64/librexruntime.dll "$WIN/librexruntime.dll"
# The SDK DLLs are built in sdk/out, not in build-win.
cp /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll "$WIN/rexgpu-xenos.dll"

PKG="/root/ht/pkg/revision-8.6-preview-$(date +%s)"
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
cp "$OUT/NOTAS-revisao-8.6-preview.md" "$OUT/RESULTADOS-GPU-revisao-8.6.md" "$SRC/reports/Reduzir uso de GPU no Xenia.md" "$BIN/"
cp "$BASE/work/appimagetool-x86_64.AppImage" "$PKG/appimagetool"
chmod +x "$PKG/appimagetool"
ARCH=x86_64 "$PKG/appimagetool" --appimage-extract-and-run "$PKG/squashfs-root" "$OUT/HydroThunder-revisao-8.6-preview.AppImage" > "$OUT/_teste_modern/package-linux-8.6.log" 2>&1
(
  cd "$BIN"
  ldd ./hydrothunder
) > "$OUT/_teste_modern/linux-dependencies-8.6.log"
if grep -q 'not found' "$OUT/_teste_modern/linux-dependencies-8.6.log"; then
  echo 'Missing dependencies' >&2
  exit 1
fi
echo "AppDir=$PKG/squashfs-root" > "$OUT/_teste_modern/linux-layout-8.6.txt"
python3 "$SRC/tools/make_zip.py" "$OUT/_teste_modern/Hydro-Thunder-Windows-revisao-8.6-preview" "$OUT/Hydro-Thunder-Windows-revisao-8.6-preview.zip"
ls -lh "$OUT/HydroThunder-revisao-8.6-preview.AppImage" "$OUT/Hydro-Thunder-Windows-revisao-8.6-preview.zip"
