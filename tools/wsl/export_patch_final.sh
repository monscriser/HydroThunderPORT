#!/usr/bin/env bash
# Exports the cumulative SDK patch (1.1.0 + RenderDoc attach-only + WaitMultiple precise sleep + texture descriptor set cache (1.2.0)) into the sources; the previous one goes to _to_delete.
set -euo pipefail
SRC=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7
P="$SRC/rexglue-modifications.patch"
echo "current: $(sha256sum "$P" | cut -c1-16)"
mkdir -p "$SRC/_to_delete"
cp -p "$P" "$SRC/_to_delete/rexglue-modifications.patch.before-final-$(date +%Y%m%d-%H%M%S)"
tr -d '\r' < "$SRC/tools/wsl/regen_patch.sh" > /root/regen.sh
bash /root/regen.sh "$SRC" /root/newfinal.patch
! grep -q 'ht_resume_running\|ht_xma_loop_end' /root/newfinal.patch
grep -q 'ht_request_range_lockless' /root/newfinal.patch
grep -q 'ht_user_profile4_' /root/newfinal.patch
grep -q 'ht_texture_set_cache' /root/newfinal.patch
grep -q 'ht_wait_precise_sleep' /root/newfinal.patch
grep -q 'RTLD_NOLOAD' /root/newfinal.patch
grep -q 'ht_shader_precompile' /root/newfinal.patch
grep -q 'ht_write_logs' /root/newfinal.patch
grep -q 'ht_progress' /root/newfinal.patch
! grep -q 'ht_diag hooks|ht_tq_pool_|ht_wm::' /root/newfinal.patch
cp /root/newfinal.patch "$P"
echo "new: $(sha256sum "$P" | cut -c1-16) files=$(grep -c '^diff --git' "$P")"
