#!/usr/bin/env bash
# Joins the rotated game logs (oldest .4 -> newest) and runs the per-track analyzer.
set -euo pipefail
D="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port/diag-ally-20w"
S="$D/diag/20261111-212142"
cat "$D/logs/hydrothunder_001.4.log" "$D/logs/hydrothunder_001.3.log" "$D/logs/hydrothunder_001.2.log" \
    "$D/logs/hydrothunder_001.1.log" "$D/logs/hydrothunder_001.log" > "$D/full-game.log"
wc -l "$D/full-game.log"
python3 -I /mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7/tools/diag/analyze_diag.py \
  "$S" --log "$D/full-game.log" --bins "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port/diag-110-linux" > /dev/null
wc -l "$S/report.md"
