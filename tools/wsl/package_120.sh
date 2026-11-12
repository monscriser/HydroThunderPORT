#!/usr/bin/env bash
# 1.2.0 BETA Linux PGO AppImage from the smoke-tested staging dir (linux_120.sh), re-extracted and
# compared byte by byte; notes + SHA256. usage: package_120.sh
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/1.2.0-BETA
TOOL=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/appimagetool-x86_64.AppImage
APPDIR=/root/ht/pkg/revision120-pgo/squashfs-root
TARGET="$OUT/HydroThunder-1.2.0-BETA-Linux-PGO.AppImage"
mkdir -p "$OUT"; rm -f "$TARGET"
cp "$SRC/NOTAS-1.2.0-BETA.md" "$APPDIR/usr/bin/"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$TARGET" > /root/ht/pkg/package-linux-120.log 2>&1
CHECK=/root/ht/pkg/check120; rm -rf "$CHECK"; mkdir -p "$CHECK"
(cd "$CHECK"; "$TARGET" --appimage-extract > extract.log)
for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so; do
  cmp "$CHECK/squashfs-root/usr/bin/$f" "/root/ht/pkg/smoke120-pgo/$f"
done
grep -q "1.2.0-beta-pgo" "$CHECK/squashfs-root/usr/bin/hydrothunder"
cp "$SRC/NOTAS-1.2.0-BETA.md" "$OUT/"
python3 -I "$SRC/tools/wsl/package_120_win.py"
(cd "$OUT"; sha256sum HydroThunder-1.2.0-BETA-Linux-PGO.AppImage Hydro-Thunder-1.2.0-BETA-Windows-PGO.zip > SHA256-1.2.0-BETA.txt; cat SHA256-1.2.0-BETA.txt; ls -la)
echo "AppImage binaries match the smoke-tested files."
