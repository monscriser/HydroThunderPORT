#!/usr/bin/env bash
# 1.2.0 BETA Linux PGO AppImage from the smoke-tested staging dir (linux_120.sh), re-extracted and
# compared byte by byte; notes + SHA256. usage: package_120.sh
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/FINAL-BUILD
TOOL=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/appimagetool-x86_64.AppImage
APPDIR=/root/ht/pkg/revisionfinal-pgo/squashfs-root
TARGET="$OUT/HydroThunder-FINAL-BUILD-Linux-PGO.AppImage"
mkdir -p "$OUT"; rm -f "$TARGET"
rm -f "$APPDIR/usr/bin/"NOTAS-*.md; cp "$SRC/NOTAS-FINAL-BUILD.md" "$APPDIR/usr/bin/"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$TARGET" > /root/ht/pkg/package-linux-120.log 2>&1
CHECK=/root/ht/pkg/checkfinal; rm -rf "$CHECK"; mkdir -p "$CHECK"
(cd "$CHECK"; "$TARGET" --appimage-extract > extract.log)
for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do
  cmp "$CHECK/squashfs-root/usr/bin/$f" "/root/ht/pkg/smokefinal-pgo/$f"
done
grep -q "final-build-pgo" "$CHECK/squashfs-root/usr/bin/hydrothunder"
cmp "$CHECK/squashfs-root/usr/bin/shaders/linux/5841096A.fbo.vk.xpso" "$SRC/shaders/linux/5841096A.fbo.vk.xpso"
cp "$SRC/NOTAS-FINAL-BUILD.md" "$OUT/"
python3 -I "$SRC/tools/wsl/package_final_win.py"
(cd "$OUT"; sha256sum HydroThunder-FINAL-BUILD-Linux-PGO.AppImage Hydro-Thunder-FINAL-BUILD-Windows-PGO.zip > SHA256-FINAL-BUILD.txt; cat SHA256-FINAL-BUILD.txt; ls -la)
echo "AppImage binaries match the smoke-tested files."
