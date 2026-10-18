#!/usr/bin/env bash
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
TASK=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane
BK="$SRC/_to_delete/antes-exportar-893-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BK"
cp "$SRC/rexglue-modifications.patch" "$BK/"
cd /root/ht/sdk
INDEX=$(mktemp /root/ht/sdk-index-893.XXXXXX)
rm "$INDEX"
export GIT_INDEX_FILE="$INDEX"
trap 'rm -f "$GIT_INDEX_FILE"' EXIT
git read-tree HEAD
git add -N .
EXCL=()
while IFS= read -r -d '' f; do EXCL+=(":(exclude)${f#./}"); done < <(cd "$SRC/sdk-extra"; find . -type f -print0)
git diff --binary --ignore-submodules=all HEAD -- . ':(exclude)out' ':(exclude,glob)thirdparty/*/**' "${EXCL[@]}" > "$SRC/rexglue-modifications.patch"
git apply --reverse --check "$SRC/rexglue-modifications.patch"
grep '^diff --git' "$SRC/rexglue-modifications.patch" > "$TASK/outputs/validacao-8.9.3/build/patch-files.txt"
! grep -q '^+.*ht_bind_ticks' "$SRC/rexglue-modifications.patch"
sha256sum "$SRC/rexglue-modifications.patch"
