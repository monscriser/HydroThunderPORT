#!/usr/bin/env bash
# Build current cumulative SDK; never restore an earlier release over it.
set -euo pipefail
TASK=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
SDK=/root/ht/sdk
OUT="${HT_893_BUILD_OUT:-$TASK/outputs/validacao-8.9.3/build}"
test ! -e "$OUT" || { echo 'Choose a new HT_893_BUILD_OUT to preserve earlier logs.'; exit 1; }
BACKUP="/root/ht/backups/revision893-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP" "$OUT"
cp "$SDK/src/graphics/vulkan/command_processor.cpp" "$BACKUP/"
cp "$SDK/out/win-amd64/rexgpu-xenos.dll" "$BACKUP/"
cp /root/ht/build-win/hydrothunder.exe "$BACKUP/"
python3 -I - "$SDK" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])/'src/graphics/vulkan/command_processor.cpp'
s=p.read_text()
for key in ('ht_zpd_batch','ht_dynamic_constants'):
    a=f'REXCVAR_DEFINE_BOOL({key}, false,'
    b=f'REXCVAR_DEFINE_BOOL({key}, true,'
    assert s.count(a)+s.count(b)==1, key
    s=s.replace(a,b)
assert 'ht_bind_ticks' not in s
assert 'REXCVAR_DEFINE_BOOL(ht_zpd_flush, false,' in s
assert 'REXCVAR_DEFINE_BOOL(ht_zpd_exact_key, true,' in s
p.write_text(s)
PY
cp "$SRC/app/src/ht_version.h" /root/ht/src/app/src/ht_version.h
export PATH=/root/ht/toolbin:/usr/bin:/bin
for TARGET in win linux; do
  APP=/root/ht/src/app
  B="/root/ht/build-$TARGET"
  if [ -e "$APP/bin" ] && [ ! -L "$APP/bin" ]; then
    mv "$APP/bin" "$BACKUP/app-bin-$TARGET"
  fi
  mkdir -p "$B/ffxbin"
  ln -sfn "$B/ffxbin" "$APP/bin"
  (cd "$B"; ninja -j12) > "$OUT/$TARGET.log" 2>&1
done
cp "$SDK/out/win-amd64/rexgpu-xenos.dll" "$TASK/work/rexgpu-xenos-zpd-batch.dll"
mkdir -p "$TASK/work/final-windows"
cp /root/ht/build-win/hydrothunder.exe "$TASK/work/final-windows/"
cp "$SDK/out/win-amd64/"*.dll "$TASK/work/final-windows/"
sha256sum "$TASK/work/final-windows/"* /root/ht/build-linux/hydrothunder "$SDK/out/linux-amd64/"*.so > "$OUT/hashes.txt"
printf '%s\n' "$BACKUP" > "$OUT/backup.txt"
echo 'Windows and Linux builds finished.'
