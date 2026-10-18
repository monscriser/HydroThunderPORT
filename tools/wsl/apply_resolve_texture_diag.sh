#!/bin/bash
# Apply ht_resolve_to_texture + its diagnostic (from the pristine backup), rebuild Windows.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
BK=/root/sdk-backup-rtt
test -d "$BK"
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_resolve_texture.py" /root/ht/sdk "$SRC/sdk-extra/ht_resolve_texture"
python3 -I "$SRC/tools/wsl/patch_resolve_texture_diag.py" /root/ht/sdk
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -30
cp /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll /mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test/rexgpu-xenos.dll
