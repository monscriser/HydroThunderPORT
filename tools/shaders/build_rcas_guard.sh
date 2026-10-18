#!/bin/bash
# Rebuilds the presenter's RCAS SPIR-V headers with the text/HUD guard.
# usage: build_rcas_guard.sh [low] [high]   (contrast thresholds, default 0.30 0.55)
set -euo pipefail
T="$(dirname "$(readlink -f "$0")")"
LOW="${1:-0.30}"; HIGH="${2:-0.55}"
S=/root/ht/sdk/src/ui/shaders/vulkan_spirv
W=/root/ht/shaders
mkdir -p "$W/orig"
for n in guest_output_ffx_fsr_rcas_ps guest_output_ffx_fsr_rcas_dither_ps; do
  # Always start from the untouched upstream bytecode.
  [ -f "$W/orig/$n.h" ] || cp "$S/$n.h" "$W/orig/$n.h"
  python3 "$T/spv_header.py" extract "$W/orig/$n.h" "$W/$n.orig.spv"
  spirv-cross --version 450 --vulkan-semantics "$W/$n.orig.spv" --output "$W/$n.orig.frag"
  python3 "$T/guard_rcas.py" "$W/$n.orig.frag" "$T/$n.frag" "$LOW" "$HIGH"
  glslangValidator -V --target-env vulkan1.0 -S frag -o "$W/$n.spv" "$T/$n.frag" >/dev/null
  spirv-val --target-env vulkan1.0 "$W/$n.spv"
  python3 "$T/spv_header.py" pack "$W/$n.spv" "$S/$n.h" "$n"
done
