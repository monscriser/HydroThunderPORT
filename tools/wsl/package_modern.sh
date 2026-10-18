#!/bin/bash
set -euo pipefail
BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT="$BASE/outputs"
P="/root/ht/pkg/modern-preview-$(date +%s)"
mkdir -p "$P"
cp "$OUT/HydroThunder-revisao-7.AppImage" "$P/base.AppImage";chmod +x "$P/base.AppImage"
(cd "$P" && ./base.AppImage --appimage-extract > extract.log)
BIN="$P/squashfs-root/usr/bin"
for f in hydrothunder librexruntime.so librexgpu-xenos.so; do
  cp "/root/ht/build-linux/$f" "$BIN/$f"
  patchelf --set-rpath '$ORIGIN' "$BIN/$f"
done
cp "$OUT/NOTAS-revisao-8-preview.md" "$OUT/REVISAO-DESEMPENHO-revisao-8.md" "$BIN/"
cp "$OUT/NOTAS-revisao-8-preview.md" "$OUT/REVISAO-DESEMPENHO-revisao-8.md" "$OUT/_teste_modern/Hydro-Thunder-Windows-modern-preview/"
cp "$BASE/work/appimagetool-x86_64.AppImage" "$P/appimagetool";chmod +x "$P/appimagetool"
ARCH=x86_64 "$P/appimagetool" --appimage-extract-and-run "$P/squashfs-root" "$OUT/HydroThunder-revisao-8-preview.AppImage" > "$OUT/_teste_modern/package-linux.log" 2>&1
python3 "$SRC/tools/make_zip.py" "$OUT/_teste_modern/Hydro-Thunder-Windows-modern-preview" "$OUT/Hydro-Thunder-Windows-revisao-8-preview.zip"
(cd "$BIN" && ldd ./hydrothunder) > "$OUT/_teste_modern/linux-dependencies.log"
if grep -q 'not found' "$OUT/_teste_modern/linux-dependencies.log"; then echo 'Missing dependencies';exit 1;fi
sha256sum "$OUT/Hydro-Thunder-Windows-revisao-8-preview.zip" "$OUT/HydroThunder-revisao-8-preview.AppImage" > "$OUT/SHA256-revisao-8-preview.txt"
echo "AppDir=$P/squashfs-root" > "$OUT/_teste_modern/linux-layout.txt"
