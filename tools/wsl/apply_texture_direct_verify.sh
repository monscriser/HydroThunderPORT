#!/bin/bash
# Add the ht_texture_direct proof mode (off by default) and rebuild Windows.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
python3 -I "$SRC/tools/wsl/patch_texture_direct_verify.py" /root/ht/sdk "$SRC/sdk-extra/ht_texture_direct"
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | tail -4
