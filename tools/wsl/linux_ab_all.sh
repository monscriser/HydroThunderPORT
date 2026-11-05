#!/usr/bin/env bash
# Runs linux_ab.sh for Standard and PGO on both tracks (alternating).
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
tr -d '\r' < "$SRC/tools/wsl/linux_ab.sh" > /root/lab.sh
for r in "final-100-linux-standard asgard S-asgard" "final-100-linux-pgo asgard P-asgard" \
         "final-100-linux-standard outra S-outra" "final-100-linux-pgo outra P-outra"; do
  set -- $r
  echo "== $3"; bash /root/lab.sh "$1" "$2" "$3" 2>&1 | tail -1
done
