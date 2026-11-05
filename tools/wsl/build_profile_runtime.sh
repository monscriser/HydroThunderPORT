#!/usr/bin/env bash
# Builds clang's profile runtime (compiler-rt/lib/profile) for Linux x86-64 from the exact LLVM
# commit of llvm-mingw's clang (23.1.3, 0d261d1c), which ships only the Windows runtime.
# Downloads only that folder (sparse, depth 1) from github.com/llvm/llvm-project.
set -euo pipefail
COMMIT=0d261d1ca552c95a8f007e061c787ac7132fbcbc
W=/root/ht/compiler-rt-profile
LM=/root/ht/llvm-mingw/bin
DEST=/root/ht/llvm-mingw/lib/clang/23/lib/x86_64-pc-linux-gnu
if [ ! -f "$W/src/compiler-rt/lib/profile/InstrProfiling.c" ]; then
  rm -rf "$W"; mkdir -p "$W/src"; cd "$W/src"
  git init -q
  git remote add origin https://github.com/llvm/llvm-project.git
  git config core.sparseCheckout true
  git sparse-checkout init --cone >/dev/null
  git sparse-checkout set compiler-rt/lib/profile compiler-rt/include/profile
  git fetch -q --depth 1 --filter=blob:none origin "$COMMIT"
  git checkout -q FETCH_HEAD
fi
cd "$W/src"
test "$(git rev-parse HEAD)" = "$COMMIT"
du -sh compiler-rt/lib/profile compiler-rt/include/profile
mkdir -p "$W/obj"; rm -f "$W/obj"/*.o
CFLAGS="--target=x86_64-pc-linux-gnu -O2 -fPIC -fno-profile-instr-generate -fvisibility=hidden -I$W/src/compiler-rt/include -I$W/src/compiler-rt/lib/profile -DCOMPILER_RT_HAS_UNAME=1 -DCOMPILER_RT_HAS_ATOMICS=1 -DCOMPILER_RT_HAS_FCNTL_LCK=1"
for f in compiler-rt/lib/profile/*.c compiler-rt/lib/profile/*.cpp; do
  case "$f" in *ROCm*) continue;; esac
  case "$f" in *.cpp) CC="$LM/clang++";; *) CC="$LM/clang";; esac
  "$CC" $CFLAGS -c "$f" -o "$W/obj/$(basename "$f").o"
done
mkdir -p "$DEST"
"$LM/llvm-ar" rcs "$DEST/libclang_rt.profile.a" "$W/obj"/*.o
echo "Built by Claude 04/11/2026 from llvm-project $COMMIT compiler-rt/lib/profile (tools/wsl/build_profile_runtime.sh) for Linux PGO builds." > "$DEST/README-claude-profile.txt"
ls -la "$DEST"
