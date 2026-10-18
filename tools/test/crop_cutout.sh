#!/bin/bash
# Side-by-side crops (rocks region, still-6): base, cutout, cutout-stencil, baseline round 3, round 4.
set -e
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
D=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_gpu/comparacao-cutout
python3 "$SRC/tools/test/crop_side.py" "$D/lado-a-lado-rochas-5.png" 760 120 1500 330 "$D/img-nativa/still-6.png" "$D/img-nativa-cutout/still-6.png" "$D/img-nativa-cutout-stencil/still-6.png" "$D/img-nativa-rodada3/still-6.png" "$D/img-nativa-rodada4/still-6.png"
