#!/usr/bin/env bash
# SDK 9.1.0 + patch_fetch_unchanged_lockless.py (B57 + B56) + patch_fetch_unchanged_verify.py
# -> Windows candidate. Originals (9.1.0) saved once per file in /root/ht/backups/ufc-ports-911.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port/${1:-cand-911-windows}"
B=/root/ht/backups/ufc-ports-911
cd /root/ht/sdk
FILES="src/graphics/vulkan/command_processor.cpp src/graphics/shared_memory.cpp src/graphics/pipeline/texture/cache.cpp include/rex/graphics/vulkan/command_processor.h src/system/xthread.cpp src/audio/xma_context.cpp"
for f in $FILES; do
  if [ ! -f "$B/$f" ]; then
    # Only files still untouched since 9.1.0 may be saved as originals.
    ! grep -q "ht_fetch_unchanged\|ht_b57\|ht_request_range_lockless\|ht_resume_running\|ht_xma_loop" "$f"
    mkdir -p "$B/$(dirname $f)"; cp -p "$f" "$B/$f"
  fi
done
# Plain cp (new mtime): with cp -p a restored original looks older than its object and ninja skips it.
for f in $FILES; do cmp -s "$B/$f" "$f" || cp "$B/$f" "$f"; done
grep -q ht_async_submit src/graphics/vulkan/command_processor.cpp
python3 -I "$SRC/tools/wsl/patch_fetch_unchanged_lockless.py" /root/ht/sdk
python3 -I "$SRC/tools/wsl/patch_fetch_unchanged_verify.py" /root/ht/sdk
# patch_runtime_a15_a3.py: measured 04/11, zero cases in Hydro (no [ht-a15]/[ht-a3] lines) -> not applied.
export PATH="/root/ht/toolbin:/usr/bin:/bin"
bash /root/ht/rebuild900-driver.sh 15 win | tail -2
mkdir -p "$OUT"
cp /root/ht/build-win/hydrothunder.exe /root/ht/sdk/out/win-amd64/librexruntime.dll /root/ht/sdk/out/win-amd64/rexgpu-xenos.dll "$OUT/"
sha256sum "$OUT"/* | cut -c1-16
