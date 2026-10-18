#!/bin/bash
# Prepares a WSL (Ubuntu 24.04) build tree in /root/ht from this source snapshot.
# usage: setup.sh <path to Hydro-Thunder-fontes-revisao-7 as seen from WSL>
set -euo pipefail
SRC="$1"
HT=/root/ht
mkdir -p "$HT"
cd "$HT"

echo "== apt"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq rsync git cmake ninja-build python3 patchelf pkg-config nasm file \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev \
  libxfixes-dev libxinerama-dev libwayland-dev libxkbcommon-dev wayland-protocols libegl-dev \
  libasound2-dev libpulse-dev libudev-dev libgl-dev libdrm-dev libgbm-dev \
  libstdc++-14-dev libgcc-14-dev >/dev/null

echo "== toolchain"
rsync -a "$SRC/../toolchain/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64/" "$HT/llvm-mingw/"
"$HT/llvm-mingw/bin/clang" --version | head -1

echo "== sdk"
REV=$(python3 -c "import json;print(json.load(open('$SRC/provenance.json'))['sdk']['revision'])")
LOCAL_SDK="$SRC/../rexglue-sdk"
if [ ! -d "$HT/sdk/.git" ]; then
  if [ -d "$LOCAL_SDK/.git" ]; then
    # Reuse the checkout (and its fetched submodules) already on this PC.
    rsync -a --exclude /out "$LOCAL_SDK/" "$HT/sdk/"
  else
    git clone -q https://github.com/rexglue/rexglue-sdk.git "$HT/sdk"
  fi
fi
cd "$HT/sdk"
git config --global --add safe.directory '*'
git checkout -q -f "$REV"
git clean -qfdx -e out
git submodule foreach --recursive -q 'git checkout -q -f . && git clean -qfdx' || true
git submodule update --init --recursive --jobs 8 -q
git apply --whitespace=nowarn "$SRC/rexglue-modifications.patch"
cp -a "$SRC/sdk-extra/." "$HT/sdk/"
if [ -s "$SRC/ffmpeg-modifications.patch" ]; then
  (cd thirdparty/FFmpeg && git checkout -q -f . && git apply --whitespace=nowarn "$SRC/ffmpeg-modifications.patch")
fi

echo "== app"
mkdir -p "$HT/src"
rm -rf "$HT/src/app"
cp -a "$SRC/app" "$HT/src/app"
cp "$SRC/windows-toolchain.cmake" "$SRC/fidelityfx-mingw-alignment.patch" "$HT/src/"
rm -rf "$HT/src/ffx-linux-build"
cp -a "$SRC/tools/ffx-linux-build" "$HT/src/ffx-linux-build"
echo "SETUP OK"
