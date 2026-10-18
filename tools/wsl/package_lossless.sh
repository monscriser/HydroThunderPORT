#!/bin/bash
set -euo pipefail
BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT="$BASE/outputs"
P="/root/ht/pkg/lsfg-preview-$(date +%s)"
mkdir -p "$P"
cd "$P"
chmod +x "$OUT/HydroThunder-revisao-7.AppImage"
"$OUT/HydroThunder-revisao-7.AppImage" --appimage-extract > extract.log
BIN="$P/squashfs-root/usr/bin"
cp /root/ht/build-linux/hydrothunder "$BIN/hydrothunder"
patchelf --set-rpath '$ORIGIN' "$BIN/hydrothunder"
cp "$OUT/LOSSLESS-SCALING-PREVIEW.md" "$BIN/LOSSLESS-SCALING.md"
cp "$BASE/work/appimagetool-x86_64.AppImage" "$P/appimagetool"
chmod +x "$P/appimagetool"
ARCH=x86_64 "$P/appimagetool" --appimage-extract-and-run "$P/squashfs-root" "$OUT/HydroThunder-Linux-LSFG-preview.AppImage" > "$OUT/_teste_lsfg/package-linux.log" 2>&1
python3 "$SRC/tools/make_zip.py" "$OUT/_teste_lsfg/Hydro-Thunder-Windows-LSFG-preview" "$OUT/Hydro-Thunder-Windows-LSFG-preview.zip"
g++ -std=c++20 -Wno-deprecated-declarations -I"$SRC/app/src" "$SRC/tools/test/lossless_discovery.cpp" -o "$OUT/_teste_lsfg/lossless-test-linux"
"$OUT/_teste_lsfg/lossless-test-linux" "$OUT/_teste_lsfg/fixture-final-$(date +%s)" > "$OUT/_teste_lsfg/discovery-linux.log"
sha256sum "$OUT/Hydro-Thunder-Windows-LSFG-preview.zip" "$OUT/HydroThunder-Linux-LSFG-preview.AppImage" > "$OUT/SHA256-LSFG-preview.txt"
echo "AppDir=$P/squashfs-root" > "$OUT/_teste_lsfg/linux-layout.txt"
