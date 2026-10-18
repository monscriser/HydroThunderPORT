#!/bin/bash
# Decompile the SDK load shaders and generate + compile the ht_texture_direct variants.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
N="texture_load_32bpb texture_load_32bpb_scaled texture_load_depth_unorm texture_load_depth_unorm_scaled texture_load_depth_float texture_load_depth_float_scaled"
tr -d '\r' < "$SRC/tools/shaders/decompile_load.sh" > /root/dl.sh
bash /root/dl.sh $N > /dev/null
python3 -I "$SRC/tools/shaders/make_direct_load.py" /root/ht/shaders/load "$SRC/sdk-extra/ht_texture_direct" $N
cd "$SRC/sdk-extra/ht_texture_direct"
for f in *.cs.glsl; do glslangValidator -V -S comp --target-env vulkan1.0 -o /root/ht/shaders/load/${f%.cs.glsl}.spv "$f" > /dev/null && echo "compiled $f"; done
