#!/usr/bin/env bash
# Shader seed (LIST only) = Ally session (Linux, all 11 tracks) + Windows PC storage + Linux test storage.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
HP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port"
g++ -O2 -std=c++17 -I /root/ht/sdk/thirdparty/xxHash "$SRC/tools/shaders/shader_seed.cpp" -o /root/ht/shader_seed
OUT="$SRC/shaders"; rm -rf "$OUT"
/root/ht/shader_seed "$OUT" \
  "$HP/ally-shader-cache/shader_cache/shaders/shareable:linux" \
  "/mnt/c/Users/Monscriser/AppData/Roaming/HydroThunderRecompiled/shader_cache/shaders/shareable:windows" \
  "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test/cache_windows/shaders/shareable:windows"
ls -la "$OUT"/*
