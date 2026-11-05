#!/usr/bin/env bash
# 9.2.0 packages: Windows zip + sources zip (package_920.py), AppImage from the smoke-tested
# staging dir, AppImage re-extracted and compared byte by byte, SHA256 list.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/revisao-9.2.0
APPDIR=/root/ht/pkg/revision920-final/squashfs-root
for f in Hydro-Thunder-Windows-revisao-9.2.0.zip Hydro-Thunder-fontes-revisao-9.2.0.zip HydroThunder-revisao-9.2.0.AppImage; do test ! -e "$OUT/$f"; done
python3 -I "$SRC/tools/wsl/package_920.py" "$OUT"
TARGET="$OUT/HydroThunder-revisao-9.2.0.AppImage"
ARCH=x86_64 /mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/appimagetool-x86_64.AppImage --appimage-extract-and-run "$APPDIR" "$TARGET" > /root/ht/pkg/package-linux-920.log 2>&1
CHECK=/root/ht/pkg/check920; rm -rf "$CHECK"; mkdir -p "$CHECK"
(cd "$CHECK"; "$TARGET" --appimage-extract > extract.log)
for f in hydrothunder librexruntime.so librexgpu-xenos.so libamd_fidelityfx_vk.so ui/Roboto-Medium.ttf; do
  cmp "$CHECK/squashfs-root/usr/bin/$f" "$APPDIR/usr/bin/$f"
done
for f in hydrothunder librexruntime.so librexgpu-xenos.so; do cmp "$APPDIR/usr/bin/$f" "/root/ht/pkg/smoke920-final/$f"; done
cp "$SRC/NOTAS-revisao-9.2.0.md" "$OUT/"
(cd "$OUT"; sha256sum Hydro-Thunder-Windows-revisao-9.2.0.zip HydroThunder-revisao-9.2.0.AppImage Hydro-Thunder-fontes-revisao-9.2.0.zip > SHA256-revisao-9.2.0.txt; cat SHA256-revisao-9.2.0.txt; ls -la)
echo 'AppImage binaries match the smoke-tested files.'
