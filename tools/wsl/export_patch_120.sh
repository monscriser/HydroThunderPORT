#!/usr/bin/env bash
# Exports the cumulative SDK patch (1.1.0 + RenderDoc attach-only + WaitMultiple precise sleep + texture descriptor set cache (1.2.0)) into the sources; the previous one goes to _to_delete.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
P="$SRC/rexglue-modifications.patch"
echo "current: $(sha256sum "$P" | cut -c1-16)"
mkdir -p "$SRC/_to_delete"
cp -p "$P" "$SRC/_to_delete/rexglue-modifications.patch.before-120-$(date +%Y%m%d-%H%M%S)"
tr -d '\r' < "$SRC/tools/wsl/regen_patch.sh" > /root/regen.sh
bash /root/regen.sh "$SRC" /root/new120.patch
! grep -q 'ht_resume_running\|ht_xma_loop_end' /root/new120.patch
grep -q 'ht_request_range_lockless' /root/new120.patch
grep -q 'ht_user_profile4_' /root/new120.patch
grep -q 'ht_texture_set_cache' /root/new120.patch
grep -q 'ht_wait_precise_sleep' /root/new120.patch
grep -q 'RTLD_NOLOAD' /root/new120.patch
! grep -q 'ht_diag hooks|ht_tq_pool_|ht_wm::' /root/new120.patch
cp /root/new120.patch "$P"
echo "new: $(sha256sum "$P" | cut -c1-16) files=$(grep -c '^diff --git' "$P")"
