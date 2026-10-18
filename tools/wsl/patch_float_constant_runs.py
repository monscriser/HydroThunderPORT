"""Reversible experiment: coalesce contiguous float4 uploads, preserve packed layout."""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
path = root / 'src/graphics/vulkan/command_processor.cpp'
text = path.read_text()
assert 'ht_float_constant_runs' not in text, 'Already patched; restore recorded backup first'
text = text.replace('#include <rex/graphics/ht_trace.h>', '#include <rex/graphics/ht_trace.h>\n#include <rex/graphics/ht_float_constant_copy.h>', 1)
flags = '''REXCVAR_DEFINE_BOOL(ht_float_constant_runs, true, "GPU/Vulkan",
                    "Copy adjacent packed shader float constants in contiguous runs");
REXCVAR_DEFINE_BOOL(ht_float_constant_runs_verify, false, "GPU/Vulkan/Debug",
                    "Compare every coalesced constant upload with the scalar reference");
REXCVAR_DEFINE_BOOL(ht_float_constant_runs_verify_break, false, "GPU/Vulkan/Debug",
                    "Positive control: perturb the comparison reference only");

'''
text = text.replace('REXCVAR_DEFINE_BOOL(ht_msaa_clear_remap', flags + 'REXCVAR_DEFINE_BOOL(ht_msaa_clear_remap', 1)
verify = '''static void HtVerifyFloatConstantCopy(const uint8_t* actual, const uint32_t* regs,
                                      const uint64_t* bitmap, uint32_t count) {
  std::array<uint8_t, 4096> expected;
  const uint8_t* end = ht::CopyFloatConstantsScalar(expected.data(), regs, bitmap);
  const size_t bytes = size_t(end - expected.data());
  if (bytes && REXCVAR_GET(ht_float_constant_runs_verify_break)) expected[0] ^= 1;
  const bool mismatch = bytes != size_t(count) * 16 ||
                        std::memcmp(actual, expected.data(), bytes) != 0;
  thread_local uint64_t calls = 0, checked_bytes = 0, mismatches = 0;
  ++calls;
  checked_bytes += bytes;
  mismatches += mismatch;
  if (calls == 1 || (calls & 4095) == 0 || (mismatch && mismatches == 1)) {
    REXLOG_WARN("[ht-fconst-verify] copies={} bytes={} mismatches={}",
                calls, checked_bytes, mismatches);
  }
}

'''
text = text.replace('bool VulkanCommandProcessor::UpdateBindings(', verify + 'bool VulkanCommandProcessor::UpdateBindings(', 1)
for suffix, base, count in (
    ('vertex', 'XE_GPU_REG_SHADER_CONSTANT_000_X', 'float_constant_count_vertex'),
    ('pixel', 'XE_GPU_REG_SHADER_CONSTANT_256_X', 'float_constant_count_pixel'),
):
    pattern = rf'      for \(uint32_t i = 0; i < 4; \+\+i\) \{{\n        uint64_t float_constant_map_entry = current_float_constant_map_{suffix}_\[i\];.*?\n      \}}\n(?=      current_constant_buffers_up_to_date_)'
    matches = list(re.finditer(pattern, text, re.S))
    assert len(matches) == 1, (suffix, len(matches))
    old = matches[0].group()
    replacement = f'''      if (REXCVAR_GET(ht_float_constant_runs)) {{
        uint8_t* const begin = mapping;
        mapping = ht::CopyFloatConstantsRuns(mapping, &regs[{base}],
                                             current_float_constant_map_{suffix}_);
        if (REXCVAR_GET(ht_float_constant_runs_verify)) {{
          HtVerifyFloatConstantCopy(begin, &regs[{base}],
                                    current_float_constant_map_{suffix}_, {count});
        }}
      }} else {{
''' + ''.join('  ' + line + '\n' for line in old.rstrip('\n').splitlines()) + '      }\n'
    text = text[:matches[0].start()] + replacement + text[matches[0].end():]
path.write_text(text)
print('Patched vertex/pixel float-constant uploads; default enabled after race validation.')
