#!/bin/bash
# Starts the portable Linux build for a few seconds and reports how far it got.
set -uo pipefail
P=/root/ht/pkg/Hydro-Thunder-Linux-revisao-7-portatil
cd "$P"
export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-x11,wayland}"
timeout 25 ./hydrothunder --ht_direct_launch=true > /root/ht/pkg/smoke.log 2>&1
echo "exit=$?"
echo "== vulkan devices"; (command -v vulkaninfo >/dev/null && vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|driverName' | head) || echo "(sem vulkaninfo)"
echo "== log"; grep -vE '^\s*$' /root/ht/pkg/smoke.log | tail -25
LOGD="$HOME/.local/share/HydroThunderRecompiled/logs"
L=$(ls -t "$LOGD"/hydrothunder_*.log 2>/dev/null | head -1)
[ -n "$L" ] && { echo "== $L"; grep -E 'ht-fsr|FidelityFX|Vulkan|error|fail' -i "$L" | head -20; }
echo "== libs carregadas"; grep -o 'libamd_fidelityfx_vk[^ ]*' /proc/self/maps 2>/dev/null | head -1 || true
