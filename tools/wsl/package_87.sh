#!/usr/bin/env bash
set -euo pipefail
BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/outputs
WORK=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/work
WIN="$WORK/Hydro-Thunder-Windows-revisao-8.7-preview"
test ! -e "$WIN"
cp -a "$BASE/outputs/_teste_modern/Hydro-Thunder-Windows-revisao-8.6-preview" "$WIN"
mkdir -p "$WORK/_to_delete"
if [ -f "$WIN/NOTAS-revisao-8.6-preview.md" ]; then mv "$WIN/NOTAS-revisao-8.6-preview.md" "$WORK/_to_delete/NOTAS-revisao-8.6.from-87-stage.md"; fi
cp "$OUT/NOTAS-revisao-8.7-preview.md" "$OUT/RESULTADOS-CPU-revisao-8.7.md" "$WIN/"
cp /root/ht/build-win/hydrothunder.exe "$WIN/data/hydrothunder.exe"
for f in librexruntime.dll rexgpu-xenos.dll; do cp "$BASE/outputs/_teste_modern/game-test/$f" "$WIN/data/$f"; done
PKG="/root/ht/pkg/revision-8.7-preview-$(date +%s)"
mkdir -p "$PKG"
cp "$BASE/outputs/HydroThunder-revisao-8.6-preview.AppImage" "$PKG/base.AppImage"
chmod +x "$PKG/base.AppImage"
(cd "$PKG"; ./base.AppImage --appimage-extract > extract.log)
BIN="$PKG/squashfs-root/usr/bin"
cp /root/ht/build-linux/hydrothunder "$BIN/hydrothunder"
for f in librexruntime.so librexgpu-xenos.so; do cp "/root/ht/sdk/out/linux-amd64/$f" "$BIN/$f"; done
for f in hydrothunder librexruntime.so librexgpu-xenos.so; do patchelf --set-rpath '$ORIGIN' "$BIN/$f"; done
cp "$OUT/NOTAS-revisao-8.7-preview.md" "$OUT/RESULTADOS-CPU-revisao-8.7.md" "$BIN/"
cp "$BASE/work/appimagetool-x86_64.AppImage" "$PKG/appimagetool"
chmod +x "$PKG/appimagetool"
test ! -e "$OUT/HydroThunder-revisao-8.7-preview.AppImage"
ARCH=x86_64 "$PKG/appimagetool" --appimage-extract-and-run "$PKG/squashfs-root" "$OUT/HydroThunder-revisao-8.7-preview.AppImage" > "$OUT/provas-cpu/build-87/package-linux.log" 2>&1
(cd "$BIN"; ldd ./hydrothunder) > "$OUT/provas-cpu/build-87/linux-dependencies.log"
if grep -q 'not found' "$OUT/provas-cpu/build-87/linux-dependencies.log"; then exit 1; fi
printf '%s\n' "$PKG/squashfs-root" > "$OUT/provas-cpu/build-87/linux-layout.txt"
python3 "$SRC/tools/wsl/finalize_87.py" "$BASE" "$SRC" "$OUT" "$WORK" "$WIN" "$BIN"