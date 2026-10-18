"""Diagnostic (frame trace only): log per resolve the copy shader, sample
selection, source MSAA and the dumped EDRAM tile span.
usage: python3 patch_resolve_trace.py /root/ht/sdk"""
import sys
from pathlib import Path
p = Path(sys.argv[1]) / "src/graphics/vulkan/render_target_cache.cpp"
s = p.read_text()
if "[ht-trace]   resolve-info" not in s:
    old = """    if (copy_shader != draw_util::ResolveCopyShaderIndex::kUnknown) {
      const draw_util::ResolveCopyShaderInfo& copy_shader_info ="""
    assert s.count(old) == 1
    s = s.replace(old, """    if (ht_trace_active) {
      uint32_t tb, trl, tr, tp;
      resolve_info.GetCopyEdramTileSpan(tb, trl, tr, tp);
      REXGPU_WARN("[ht-trace]   resolve-info shader={} sample_select={} depth_msaa={} color_msaa={} "
                  "is_depth={} span base={} row_len={} rows={} pitch={} groups={}x{}",
                  uint32_t(copy_shader), uint32_t(resolve_info.rb_copy_control.copy_sample_select),
                  uint32_t(resolve_info.depth_edram_info.msaa_samples),
                  uint32_t(resolve_info.color_edram_info.msaa_samples),
                  uint32_t(resolve_info.IsCopyingDepth()), tb, trl, tr, tp, copy_group_count_x,
                  copy_group_count_y);
    }
""" + old, 1)
    p.write_text(s)
print("patched")
