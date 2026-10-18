#!/bin/bash
# Show EDRAM-buffer load sites and push constant blocks of decompiled resolve shaders.
cd /root/ht/shaders/load
for f in resolve_*.comp; do
  echo "== $f"
  grep -n 'readonly buffer\|writeonly buffer' "$f"
  edram=$(grep -B0 -A3 'set = 0, binding = 0, std430) readonly buffer' "$f" | grep -o '} _[0-9]*;' | tr -d '}; ')
  echo "edram var: $edram loads: $(grep -c "$edram\._m0\[" "$f")"
  grep -o "$edram\._m0\[[^]]*\]" "$f" | sort | uniq | head
  sed -n '/push_constant/,/}/p' "$f"
done
