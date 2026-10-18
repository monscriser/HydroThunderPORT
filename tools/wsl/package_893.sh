#!/usr/bin/env bash
set -euo pipefail
TASK=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OLD=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/as-instru-es-para-outro-agente
APPDIR=/root/ht/pkg/revision893-final/squashfs-root
OUT="$TASK/outputs"
BIN="$APPDIR/usr/bin"
python3 -I "$TASK/work/package_893.py" "$SRC" "$OUT" "$TASK/work" "$OLD/outputs" "$OLD/work" "$APPDIR"
mkdir -p /root/ht/pkg/revision893-final/old-notes
for f in "$BIN"/NOTAS-revisao-*.md; do
  if [ -f "$f" ]; then mv "$f" /root/ht/pkg/revision893-final/old-notes/; fi
done
cp "$OUT/NOTAS-revisao-8.9.3-preview.md" "$BIN/"
TARGET="$OUT/HydroThunder-revisao-8.9.3-preview.AppImage"
test ! -e "$TARGET"
TOOL=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/appimagetool-x86_64.AppImage
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$TARGET" > "$OUT/validacao-8.9.3/build/package-linux.log" 2>&1
CHECK=/root/ht/pkg/check893-final
test ! -e "$CHECK"; mkdir -p "$CHECK"
(cd "$CHECK"; "$TARGET" --appimage-extract > extract.log)
for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do
  cmp "$CHECK/squashfs-root/usr/bin/$f" "/root/ht/pkg/smoke893-final/$f"
done
(cd "$OUT"; sha256sum Hydro-Thunder-Windows-revisao-8.9.3-preview.zip HydroThunder-revisao-8.9.3-preview.AppImage Hydro-Thunder-fontes-revisao-8.9.3-preview.zip > SHA256-revisao-8.9.3-preview.txt)
echo 'Windows, AppImage and source packages complete; AppImage binaries match smoke-tested files.'
