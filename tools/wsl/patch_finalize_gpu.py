"""Finalize the GPU work in the SDK:
- remove the temporary [ht-remap-why] log;
- [ht-xfer]/[ht-texload] only with HT_GPU_DIAG=1 (ht_perf_log is on in normal play);
- ht_msaa_clear_remap on by default (validated: same image, -24% GPU at Nativa).

usage: python3 patch_finalize_gpu.py /root/ht/sdk
"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
cp = root / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
start = s.find("    if (surface_info.msaa_samples == xenos::MsaaSamples::k4X &&\n        initiator.prim_type == xenos::PrimitiveType::kRectangleList) {\n      static uint32_t ht_why_count")
if start >= 0:
    end = s.index("    if (surface_info.msaa_samples == xenos::MsaaSamples::k4X &&\n        initiator.prim_type == xenos::PrimitiveType::kRectangleList && initiator.num_indices == 3", start)
    s = s[:start] + s[end:]
s = s.replace('REXCVAR_DEFINE_BOOL(ht_msaa_clear_remap, false, "GPU",', 'REXCVAR_DEFINE_BOOL(ht_msaa_clear_remap, true, "GPU",', 1)
assert "[ht-remap-why]" not in s
cp.write_text(s)

gate = '''static bool HtGpuDiag() {
  static const bool enabled = [] {
    const char* v = std::getenv("HT_GPU_DIAG");
    return v && v[0] == '1';
  }();
  return enabled;
}
'''
for rel, tag in (("src/graphics/vulkan/render_target_cache.cpp", "[ht-xfer]"),
                 ("src/graphics/vulkan/texture_cache.cpp", "[ht-texload]")):
    p = root / rel
    t = p.read_text()
    if "HtGpuDiag()" not in t:
        old = "  if (REXCVAR_GET(ht_perf_log)) {\n    static std::map<std::string"
        assert old in t, rel
        t = t.replace(old, "  if (HtGpuDiag()) {\n    static std::map<std::string", 1)
        anchor = "namespace rex::graphics::vulkan {"
        if anchor not in t:
            anchor = "namespace rex::graphics {"
        assert anchor in t, rel
        t = t.replace(anchor, anchor + "\n// HT_GPU_DIAG=1: list EDRAM transfers / texture loads (diagnostics only).\n" + gate, 1)
        p.write_text(t)
print("finalized")
