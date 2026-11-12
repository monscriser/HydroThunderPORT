#!/usr/bin/env bash
# texture descriptor set cache: build once, then off / on / verify / verify_break under llvmpipe.
set -uo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
export PATH="/root/ht/toolbin:/usr/bin:/bin"
BK=/root/ht/backups/diag-110
cd /root/ht/sdk
: cp "$BK/src/graphics/vulkan/command_processor.cpp" src/graphics/vulkan/command_processor.cpp
: cp "$BK/include/rex/graphics/vulkan/command_processor.h" include/rex/graphics/vulkan/command_processor.h
: skip
: skip
tr -d '\r' < "$SRC/tools/wsl/diag/cpu_smoke_linux.sh" > /root/cs.sh
for v in off on verify break; do
  case $v in
    off) C='ht_perf_log=true;ht_texture_set_cache=false';;
    on) C='ht_perf_log=true;ht_texture_set_cache=true';;
    verify) C='ht_perf_log=true;ht_texture_set_cache=true;ht_texture_set_cache_verify=true';;
    break) C='ht_perf_log=true;ht_texture_set_cache=true;ht_texture_set_cache_verify=true;ht_texture_set_cache_verify_break=true';;
  esac
  echo "===== $v"
  bash /root/cs.sh tex-$v 150 "ht-bind|ht-draw" HT_BOOT_TEST=1 "HT_TEST_CVARS=$C" | grep -v "%" | tail -3
done
