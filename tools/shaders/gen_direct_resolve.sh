#!/bin/bash
# Decompile the SDK resolve copy shaders and generate + compile the ht_direct_resolve variants.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
N="resolve_fast_32bpp_1x2xmsaa resolve_fast_32bpp_1x2xmsaa_scaled resolve_fast_32bpp_4xmsaa resolve_fast_32bpp_4xmsaa_scaled resolve_full_32bpp resolve_full_32bpp_scaled"
tr -d '\r' < "$SRC/tools/shaders/decompile_load.sh" > /root/dl.sh
bash /root/dl.sh $N > /dev/null
OUT="$SRC/sdk-extra/ht_direct_resolve"
python3 -I "$SRC/tools/shaders/make_direct_resolve.py" /root/ht/shaders/load "$OUT"
cd "$OUT"
ok=0
for f in *.cs.glsl; do
  glslangValidator -V -S comp --target-env vulkan1.0 -o /root/ht/shaders/load/${f%.cs.glsl}.spv "$f" > /tmp/gl.log 2>&1 && ok=$((ok+1)) || { echo "FAIL $f"; head -5 /tmp/gl.log; }
done
echo "compiled $ok"
