#!/bin/bash
set -euo pipefail
BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT="$BASE/outputs/_teste_modern"
mkdir -p "$OUT"
python3 - "$BASE" <<'PY'
import zipfile,sys
from pathlib import Path
base=Path(sys.argv[1]);dest=base/'outputs/_teste_modern/Hydro-Thunder-Windows-modern-preview'
with zipfile.ZipFile(base/'outputs/Hydro-Thunder-Windows-revisao-7.zip') as z:z.extractall(dest)
PY
WIN="$OUT/Hydro-Thunder-Windows-modern-preview/data"
cp /root/ht/build-win/{hydrothunder.exe,librexruntime.dll,rexgpu-xenos.dll} "$WIN/"
cp "$SRC/app/src/ht_lossless.h" /root/ht/src/app/src/
tr -d '\r' < "$SRC/tools/wsl/regen_patch.sh" > /root/regen_modern_patch.sh
bash /root/regen_modern_patch.sh "$SRC" "$SRC/rexglue-modifications.patch"
