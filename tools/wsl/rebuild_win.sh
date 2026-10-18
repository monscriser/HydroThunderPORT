#!/bin/bash
# Incremental rebuild in WSL (after build.sh has configured the target once).
# usage: rebuild_win.sh [jobs] [win|linux]
set -euo pipefail
HT=/root/ht
APP="$HT/src/app"
T="${2:-win}"
B="$HT/build-$T"
test -d "$APP" && test -d "$B"
mkdir -p "$B/ffxbin"
# FidelityFX writes static libs to $APP/bin: give each target its own directory.
if [ -d "$APP/bin" ] && [ ! -L "$APP/bin" ]; then
  rm -rf -- "$APP/bin"
fi
ln -sfn "$B/ffxbin" "$APP/bin"
export PATH="$HT/toolbin:/usr/bin:/bin"
cd "$B"
ninja -j"${1:-15}" > build2.log 2>&1 || { grep -E 'error|FAILED' build2.log | head -20; exit 1; }
tail -1 build2.log
ls -la --time-style=+%T "$B"/hydrothunder* "$B"/*.dll "$B"/*.so 2>/dev/null || true
