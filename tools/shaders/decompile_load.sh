#!/bin/bash
# Decompile SDK texture load shaders to GLSL for study (output in /root/ht/shaders/load).
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
mkdir -p /root/ht/shaders/load && cd /root/ht/shaders/load
for n in "$@"; do
  python3 "$SRC/tools/shaders/h2spv.py" "/root/ht/sdk/src/graphics/shaders/vulkan_spirv/${n}_cs.h" "$n.spv"
  spirv-cross -V "$n.spv" --output "$n.comp"
done
wc -l *.comp
