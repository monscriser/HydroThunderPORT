#!/usr/bin/env bash
# 1.0.0 BETA packages: 2 Windows zips + sources (package_100.py), 2 AppImages from the smoke-tested
# staging dirs (re-extracted and compared byte by byte), SHA256 list.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/1.0.0-BETA
test ! -e "$OUT/SHA256-1.0.0-BETA.txt"
python3 -I "$SRC/tools/wsl/package_100.py" "$OUT"
TOOL=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/appimagetool-x86_64.AppImage
for V in standard pgo; do
  L=$([ $V = pgo ] && echo PGO || echo Standard)
  APPDIR=/root/ht/pkg/revision100-$V/squashfs-root
  TARGET="$OUT/HydroThunder-1.0.0-BETA-Linux-$L.AppImage"
  test ! -e "$TARGET"
  ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$TARGET" > /root/ht/pkg/package-linux-100-$V.log 2>&1
  CHECK=/root/ht/pkg/check100-$V; rm -rf "$CHECK"; mkdir -p "$CHECK"
  (cd "$CHECK"; "$TARGET" --appimage-extract > extract.log)
  for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so ui/Roboto-Medium.ttf; do
    cmp "$CHECK/squashfs-root/usr/bin/$f" "$APPDIR/usr/bin/$f"
  done
  for f in hydrothunder librexruntime.so librexgpu-xenos.so; do cmp "$APPDIR/usr/bin/$f" "/root/ht/pkg/smoke100-$V/$f"; done
  grep -q "1.0.0-beta-$V" "$CHECK/squashfs-root/usr/bin/hydrothunder"
done
cp "$SRC/NOTAS-1.0.0-BETA.md" "$OUT/"
(cd "$OUT"; sha256sum Hydro-Thunder-1.0.0-BETA-Windows-Standard.zip Hydro-Thunder-1.0.0-BETA-Windows-PGO.zip \
  HydroThunder-1.0.0-BETA-Linux-Standard.AppImage HydroThunder-1.0.0-BETA-Linux-PGO.AppImage \
  Hydro-Thunder-1.0.0-BETA-fontes.zip > SHA256-1.0.0-BETA.txt; cat SHA256-1.0.0-BETA.txt; ls -la)
echo 'AppImage binaries match the smoke-tested files.'
