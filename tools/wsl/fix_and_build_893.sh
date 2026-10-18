#!/usr/bin/env bash
set -euo pipefail
TASK=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane
BK="$TASK/work/_to_delete/antes-corrigir-prova-escalada"
mkdir -p "$BK"
cp /root/ht/sdk/src/graphics/vulkan/texture_cache.cpp "$BK/"
cp -a "$TASK/work/final-windows" "$BK/"
python3 -I "$TASK/work/patch_scaled_resolve_verify_usage.py" /root/ht/sdk
tr -d '\r' < "$TASK/work/build_893.sh" > /root/ht/build893-final2.sh
bash /root/ht/build893-final2.sh
mkdir -p /root/ht/pkg/_to_delete
mv /root/ht/pkg/revision893-final /root/ht/pkg/_to_delete/revision893-first
mv /root/ht/pkg/smoke893-final /root/ht/pkg/_to_delete/smoke893-first
tr -d '\r' < "$TASK/work/prepare_linux_893.sh" > /root/ht/prepare893-final2.sh
bash /root/ht/prepare893-final2.sh
tr -d '\r' < "$TASK/work/export_patch_893.sh" > /root/ht/export893-final2.sh
bash /root/ht/export893-final2.sh
