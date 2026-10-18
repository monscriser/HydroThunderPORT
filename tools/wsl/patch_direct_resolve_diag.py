"""Diagnostic (HT_GPU_DIAG=1 only): count why resolves do not take the
ht_direct_resolve path, log [ht-dres-why] every 10 s.
usage: python3 patch_direct_resolve_diag.py /root/ht/sdk"""
import re
import sys
from pathlib import Path
p = Path(sys.argv[1]) / "src/graphics/vulkan/render_target_cache.cpp"
s = p.read_text()
if "[ht-dres-why]" not in s:
    start = s.index("bool VulkanRenderTargetCache::HtDirectResolvePrepare(")
    end = s.index("\n}\n", start)
    body = s[start:end]
    reasons = []
    def repl(m):
        reasons.append(len(reasons))
        return f"{m.group(1)}return HtWhy({len(reasons) - 1});"
    body2 = re.sub(r"(\n\s*|if \(is_uint\) )return false;", repl, body)
    helper = '''static bool HtWhy(uint32_t reason) {
  static uint64_t counts[32] = {};
  static auto t0 = std::chrono::steady_clock::now();
  ++counts[reason & 31];
  const auto now = std::chrono::steady_clock::now();
  if (HtGpuDiag() && now - t0 >= std::chrono::seconds(10)) {
    t0 = now;
    std::string line;
    for (uint32_t i = 0; i < 32; ++i) {
      if (counts[i]) line += fmt::format(" r{}={}", i, counts[i]);
    }
    REXGPU_WARN("[ht-dres-why]{}", line);
  }
  return false;
}

'''
    s = s[:start] + helper + body2 + s[end:]
    p.write_text(s)
    print("patched, reasons:", len(reasons))
