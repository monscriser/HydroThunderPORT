#!/bin/bash
# Builds and runs tools/test/test_steam_export.cpp (Linux) against a fake Steam tree.
set -euo pipefail
SRC="$1"
T=/root/ht/steamtest
rm -rf "$T"; mkdir -p "$T/game/steam_art" "$T/game/ui"
cp -r "$SRC"/steam_art/. "$T/game/steam_art/"
cp /root/ht/build-linux/hydrothunder "$T/game/hydrothunder"
LM=/root/ht/llvm-mingw/bin
"$LM/clang++" --target=x86_64-pc-linux-gnu -fuse-ld=lld -std=c++20 -O1 \
  -o "$T/test" "$SRC/tools/test/test_steam_export.cpp"
"$T/test" "$T/game" "$T/scratch"
