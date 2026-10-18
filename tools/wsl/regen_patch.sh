#!/bin/bash
# Regenerates rexglue-modifications.patch from the WSL SDK tree (sdk-extra files excluded).
# usage: regen_patch.sh <source snapshot dir as seen from WSL> <output patch>
set -euo pipefail
SRC="$1"; OUT="$2"
cd /root/ht/sdk
EXTRA=$(cd "$SRC/sdk-extra" && find . -type f | sed 's|^\./||')
git add -N . >/dev/null 2>&1 || true
EXCL=()
for f in $EXTRA; do EXCL+=(":(exclude)$f"); done
git diff --binary --ignore-submodules=all HEAD -- . ':(exclude)out' ':(exclude,glob)thirdparty/*/**' "${EXCL[@]}" > "$OUT"
git reset -q
echo "files: $(grep -c '^diff --git' "$OUT")"
