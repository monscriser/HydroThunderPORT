#!/usr/bin/env bash
# Diagnostic AppImage from the smoke-tested AppDir (binaries compared byte by byte).
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/1.1.0-DIAG
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
TOOL=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/appimagetool-x86_64.AppImage
APPDIR=/root/ht/pkg/revision110-diag/squashfs-root
TARGET="$OUT/HydroThunder-1.1.0-DIAG-Linux-PGO.AppImage"
python3 -I "$SRC/tools/diag/analyze_diag.py" "$HP/linux-llvmpipe-110-diag/session" --bins "$HP/diag-110-linux" > /dev/null && echo "analyzer OK on smoke session"
mkdir -p "$OUT"; rm -f "$TARGET"
cp "$SRC/tools/diag/LEIA-ME-DIAGNOSTICO.md" "$APPDIR/usr/bin/"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$TARGET" > /root/ht/pkg/package-linux-diag.log 2>&1
CHECK=/root/ht/pkg/checkdiag; rm -rf "$CHECK"; mkdir -p "$CHECK"
(cd "$CHECK"; "$TARGET" --appimage-extract > extract.log)
for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do
  cmp "$CHECK/squashfs-root/usr/bin/$f" "/root/ht/pkg/diagtest/run-diag/$f"
done
grep -q "1.1.0-beta-diag" "$CHECK/squashfs-root/usr/bin/hydrothunder"
cp "$SRC/tools/diag/LEIA-ME-DIAGNOSTICO.md" "$SRC/tools/diag/analyze_diag.py" "$OUT/"
cp "$HP/linux-llvmpipe-110-diag/RENDERDOC-LOADED.txt" "$OUT/prova-renderdoc-1.1.0-carregou.txt"
cp "$HP/linux-llvmpipe-110-diag/session/report.md" "$OUT/exemplo-relatorio-llvmpipe.md"
(cd "$OUT"; sha256sum HydroThunder-1.1.0-DIAG-Linux-PGO.AppImage > SHA256-1.1.0-DIAG.txt; cat SHA256-1.1.0-DIAG.txt; ls -la)
echo "AppImage binaries match the smoke-tested files."
