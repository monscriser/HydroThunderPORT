#!/bin/bash
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
OUT=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_lsfg/first-run-$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"
for f in ht_lossless.h ht_setup_wizard.cpp ht_app_common.h ht_version.h main.cpp ht_video_policy.h ht_config_edit.h ht_settings_overlay.cpp ht_fps_overlay.cpp; do cp "$SRC/app/src/$f" /root/ht/src/app/src/; done
g++ -std=c++20 -I"$SRC/app/src" "$SRC/tools/test/lossless_discovery.cpp" -o "$OUT/lossless-test-linux"
"$OUT/lossless-test-linux" "$OUT/fixture-linux-$(date +%s)" > "$OUT/discovery-linux.log"
"$OUT/lossless-test-linux" --report > "$OUT/installed-linux.log"
/root/ht/llvm-mingw/bin/x86_64-w64-mingw32-clang++ -std=c++20 -static -I"$SRC/app/src" "$SRC/tools/test/lossless_discovery.cpp" -ladvapi32 -o "$OUT/lossless-test.exe"
export PATH=/root/ht/toolbin:/usr/bin:/bin
for target in win linux; do
  B=/root/ht/build-$target
  mkdir -p "$B/ffxbin"
  if [ -d /root/ht/src/app/bin ] && [ ! -L /root/ht/src/app/bin ]; then
    mv /root/ht/src/app/bin "/root/ht/src/app/bin_to_delete_$(date +%s)"
  fi
  ln -sfn "$B/ffxbin" /root/ht/src/app/bin
  (cd "$B" && ninja -j12 hydrothunder > "$OUT/build-$target.log" 2>&1) || { tail -40 "$OUT/build-$target.log";exit 1; }
  tail -3 "$OUT/build-$target.log"
done
