#!/bin/bash
# Generate (from sdk-extra/ht_direct_resolve) and test-compile the ht_resolve_texture shaders.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT="$SRC/sdk-extra/ht_resolve_texture"
python3 -I "$SRC/tools/shaders/make_resolve_texture.py" "$SRC/sdk-extra/ht_direct_resolve" "$OUT"
mkdir -p /root/ht/shaders/rtt
cd "$OUT"
ok=0
for f in *.cs.glsl; do
  glslangValidator -V -S comp --target-env vulkan1.0 -o /root/ht/shaders/rtt/${f%.cs.glsl}.spv "$f" > /tmp/gl.log 2>&1 && ok=$((ok+1)) || { echo "FAIL $f"; head -8 /tmp/gl.log; }
done
echo "compiled $ok"
