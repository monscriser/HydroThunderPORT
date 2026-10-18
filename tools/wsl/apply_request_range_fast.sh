#!/bin/bash
# ht_request_range_fast on the pristine SDK files (backup /root/sdk-backup-reqrange, made by
# apply_measure_reqrange.sh). With "measure" also applies the measurement-only timer.
# Rebuilds Windows; the DLL is NOT copied anywhere (the caller backs up and swaps it).
# usage: apply_request_range_fast.sh [measure]
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
BK=/root/sdk-backup-reqrange
test -d "$BK"
cp -r "$BK"/* /root/ht/sdk/
python3 -I "$SRC/tools/wsl/patch_request_range_fast.py" /root/ht/sdk
# The timer goes in after the fast path patch so it sits first in RequestRanges and
# times every call, fast ones included.
if [ "${1:-}" = measure ]; then python3 -I "$SRC/tools/wsl/patch_measure_reqrange.py" /root/ht/sdk; fi
tr -d '\r' < "$SRC/tools/wsl/rebuild_win.sh" > /root/rw.sh
bash /root/rw.sh 15 win 2>&1 | grep -E 'error|Linking|FAILED|ninja: no work' | head -30
sha256sum /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll /root/ht/sdk/out/win-amd64/librexruntime.dll
