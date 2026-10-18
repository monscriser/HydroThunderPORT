#!/bin/bash
# Extracts the presenter's RCAS SPIR-V headers and decompiles them to GLSL.
set -euo pipefail
T="$(dirname "$(readlink -f "$0")")"
S=/root/ht/sdk/src/ui/shaders/vulkan_spirv
W=/root/ht/shaders
mkdir -p "$W"; cd "$W"
head -c 300 "$S/guest_output_ffx_fsr_rcas_ps.h"; echo
for n in guest_output_ffx_fsr_rcas_ps guest_output_ffx_fsr_rcas_dither_ps; do
  python3 "$T/spv_header.py" extract "$S/$n.h" "$n.spv"
  spirv-cross --version 450 --vulkan-semantics "$n.spv" --output "$n.frag"
  wc -l "$n.frag"
done
