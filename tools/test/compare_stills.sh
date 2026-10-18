#!/bin/bash
# usage: compare_stills.sh <comparison dir (WSL path)>  - runs scale_compare.py for still-0/3/6.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
D="$1"
for s in still-0.png still-3.png still-6.png; do
  python3 "$SRC/tools/test/scale_compare.py" "$D" "$s" > /dev/null
  cp "$D/metrics.json" "$D/metrics-${s%.png}.json"
  python3 - "$D/metrics.json" "$s" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
print(sys.argv[2], {k: {r: v['ssim'] for r, v in d.items()} for k, d in m.items() if k != 'img-nativa'})
PY
done
