#!/usr/bin/env bash
set -euo pipefail
BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/outputs
WORK=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente/work
WIN="$WORK/Hydro-Thunder-Windows-revisao-8.9.1-preview"
test ! -e "$WIN"
cp -a "$WORK/Hydro-Thunder-Windows-revisao-8.9-preview" "$WIN"
mkdir -p "$WORK/_to_delete"
if [ -f "$WIN/NOTAS-revisao-8.9-preview.md" ]; then mv "$WIN/NOTAS-revisao-8.9-preview.md" "$WORK/_to_delete/NOTAS-revisao-8.9.from-891-stage.md"; fi
cp "$OUT/NOTAS-revisao-8.9.1-preview.md" "$WIN/"
cp /root/ht/build-win/hydrothunder.exe "$WIN/data/hydrothunder.exe"
for f in librexruntime.dll rexgpu-xenos.dll; do cp "$BASE/outputs/_teste_modern/game-test/$f" "$WIN/data/$f"; done
PKG="/root/ht/pkg/revision-8.9.1-preview-$(date +%s)"
mkdir -p "$PKG"
cp "$OUT/HydroThunder-revisao-8.9-preview.AppImage" "$PKG/base.AppImage"
chmod +x "$PKG/base.AppImage"
(cd "$PKG"; ./base.AppImage --appimage-extract > extract.log)
BIN="$PKG/squashfs-root/usr/bin"
cp /root/ht/build-linux/hydrothunder "$BIN/hydrothunder"
for f in librexruntime.so librexgpu-xenos.so; do cp "/root/ht/sdk/out/linux-amd64/$f" "$BIN/$f"; done
for f in hydrothunder librexruntime.so librexgpu-xenos.so; do patchelf --set-rpath '$ORIGIN' "$BIN/$f"; done
cp "$OUT/NOTAS-revisao-8.9.1-preview.md" "$BIN/"
cp "$BASE/work/appimagetool-x86_64.AppImage" "$PKG/appimagetool"
chmod +x "$PKG/appimagetool"
test ! -e "$OUT/HydroThunder-revisao-8.9.1-preview.AppImage"
ARCH=x86_64 "$PKG/appimagetool" --appimage-extract-and-run "$PKG/squashfs-root" "$OUT/HydroThunder-revisao-8.9.1-preview.AppImage" > "$OUT/otimizacao-8.9/build-891/package-linux.log" 2>&1
(cd "$BIN"; ldd ./hydrothunder) > "$OUT/otimizacao-8.9/build-891/linux-dependencies.log"
if grep -q 'not found' "$OUT/otimizacao-8.9/build-891/linux-dependencies.log"; then exit 1; fi
printf '%s\n' "$PKG/squashfs-root" > "$OUT/otimizacao-8.9/build-891/linux-layout.txt"
python3 "$SRC/tools/wsl/finalize_891.py" "$BASE" "$SRC" "$OUT" "$WORK" "$WIN" "$BIN"