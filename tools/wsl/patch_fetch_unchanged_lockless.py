#!/usr/bin/env python3
"""Hydro 9.1.x: two command-thread CPU cuts ported from UFC3MONS BETA 1.2 (B57, B56).

B57 ht_fetch_unchanged_skip: a fetch constant rewritten with the SAME value no longer clears the
  fetch constant buffer bit, the texture binding sync bit or the vertex buffer residency bit. The
  inputs of all three are only the register values (texture memory changes are tracked separately
  by texture_became_outdated_, vertex data by the submission/frame resets). The bulk path
  (WriteRegistersFromMem) invalidates exactly the changed dwords, the same bits the per-register
  path clears, so the ht_cp_fast_registers verifier still compares equal.
  [ht-b57] fetch_writes / unchanged counters (power-of-two writes).
B56 ht_request_range_lockless: the single-range valid-page check reads the valid bits with an
  atomic load instead of taking the global critical region (contended by guest threads). A page
  the guest writes is invalidated under the lock before the write completes and before the guest
  submits the commands that use it. Proof mode (ht_request_range_fast_verify) still holds the lock
  from the check through the re-run of the original path, so the proof stays exact.
Usage: patch_fetch_unchanged_lockless.py <sdk root>"""
import sys, os

sdk = sys.argv[1]
CP = os.path.join(sdk, "src/graphics/vulkan/command_processor.cpp")
SM = os.path.join(sdk, "src/graphics/shared_memory.cpp")


def edit(path, pairs):
    s = open(path, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    s = s.replace("\r\n", "\n")
    for old, new in pairs:
        n = s.count(old)
        if n != 1:
            sys.exit(f"{os.path.basename(path)}: anchor found {n} times: {old[:80]!r}")
        s = s.replace(old, new)
    if crlf:
        s = s.replace("\n", "\r\n")
    open(path, "w", encoding="utf-8", newline="").write(s)
    print("patched", path)


CVARS_CP = '''REXCVAR_DEFINE_BOOL(ht_fetch_unchanged_skip, false, "GPU/Vulkan",
                    "B57: a fetch constant rewritten with the same value keeps texture/vertex bindings "
                    "(exact, but only ~6% of rewrites are identical in Hydro: no measured gain, off)");
'''

HELPER_CP = '''// B57: counts fetch constant writes; true when an unchanged write may skip the invalidation.
static bool HtFetchConstantUnchanged(bool unchanged) {
  static uint64_t writes = 0, same = 0;  // command processor thread only
  const uint64_t w = ++writes;
  if (unchanged) {
    ++same;
  }
  if ((w & (w - 1)) == 0 && w >= 65536) {
    REXGPU_WARN("[ht-b57] fetch_writes={} unchanged={} skip={}", w, same,
                REXCVAR_GET(ht_fetch_unchanged_skip));
  }
  return unchanged && REXCVAR_GET(ht_fetch_unchanged_skip);
}

void VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  // B57: remember the old value so an unchanged fetch constant keeps the bindings.
  const uint32_t ht_old_value =
      index < RegisterFile::kRegisterCount ? register_file_->values[index] : ~value;
  CommandProcessor::WriteRegister(index, value);
'''

edit(CP, [
    ('REXCVAR_DEFINE_BOOL(ht_async_submit, true,', CVARS_CP + 'REXCVAR_DEFINE_BOOL(ht_async_submit, true,'),
    ('void VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {\n'
     '  CommandProcessor::WriteRegister(index, value);\n', HELPER_CP),
    ('             index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {\n'
     '    current_constant_buffers_up_to_date_ &=\n'
     '        ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch);\n'
     '    if (texture_cache_) {\n'
     '      texture_cache_->TextureFetchConstantWritten((index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) /\n',
     '             index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {\n'
     '    if (HtFetchConstantUnchanged(ht_old_value == value)) {\n'
     '      return;\n'
     '    }\n'
     '    current_constant_buffers_up_to_date_ &=\n'
     '        ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch);\n'
     '    if (texture_cache_) {\n'
     '      texture_cache_->TextureFetchConstantWritten((index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) /\n'),
    ('''      end_index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    memory::copy_and_swap(register_file_->values + start_index, base, num_registers);
    current_constant_buffers_up_to_date_ &=
        ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch);
    uint32_t first_fetch_dword = start_index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    uint32_t last_fetch_dword = end_index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    if (texture_cache_) {
      texture_cache_->TextureFetchConstantsWritten(first_fetch_dword / 6, last_fetch_dword / 6);
    }
    InvalidateVertexBufferResidencyRange(first_fetch_dword / 2, last_fetch_dword / 2);
    return;
  }
''', '''      end_index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    if (REXCVAR_GET(ht_fetch_unchanged_skip)) {
      // B57: invalidate exactly the dwords that change (the bits the per-register path clears).
      uint32_t* values = register_file_->values + start_index;
      bool any_changed = false;
      for (uint32_t i = 0; i < num_registers; ++i) {
        const uint32_t value = memory::load_and_swap<uint32_t>(base + i);
        if (values[i] == value) {
          continue;
        }
        // Same bits WriteRegister clears for this dword.
        values[i] = value;
        any_changed = true;
        const uint32_t dword = start_index + i - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
        if (texture_cache_) {
          texture_cache_->TextureFetchConstantWritten(dword / 6);
        }
        InvalidateVertexBufferResidency(dword / 2);
      }
      if (any_changed) {
        current_constant_buffers_up_to_date_ &=
            ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch);
      }
      HtFetchConstantUnchanged(!any_changed);
      return;
    }
    memory::copy_and_swap(register_file_->values + start_index, base, num_registers);
    current_constant_buffers_up_to_date_ &=
        ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch);
    uint32_t first_fetch_dword = start_index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    uint32_t last_fetch_dword = end_index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    if (texture_cache_) {
      texture_cache_->TextureFetchConstantsWritten(first_fetch_dword / 6, last_fetch_dword / 6);
    }
    InvalidateVertexBufferResidencyRange(first_fetch_dword / 2, last_fetch_dword / 2);
    return;
  }
'''),
])

CVARS_SM = '''REXCVAR_DEFINE_BOOL(ht_request_range_lockless, true, "GPU",
                    "B56: single-range valid-page check without the global lock");
'''

edit(SM, [
    ('REXCVAR_DEFINE_BOOL(ht_request_range_fast_verify_break, false, "GPU",',
     CVARS_SM + 'REXCVAR_DEFINE_BOOL(ht_request_range_fast_verify_break, false, "GPU",'),
    ('''    bool all_valid = true;
    {
      auto global_lock = global_critical_region_.Acquire();
''', '''    bool all_valid = true;
    {
      // B56: without the global critical region (guest threads contend for it). A page the guest
      // writes is invalidated under the lock before the write completes, and the guest submits
      // the commands that use the data after writing it, so this read sees the invalidation.
      const bool lockless = REXCVAR_GET(ht_request_range_lockless);
      std::unique_lock<std::recursive_mutex> global_lock;
      if (!lockless) {
        global_lock = global_critical_region_.Acquire();
      }
'''),
    ('''        if ((system_page_flags_valid_[i] & mask) != mask) {
          all_valid = false;
          break;
        }
      }
    }
    if (all_valid) {
      if (REXCVAR_GET(ht_request_range_fast_verify)) {''', '''        const uint64_t block_valid =
            lockless ? __atomic_load_n(&system_page_flags_valid_[i], __ATOMIC_ACQUIRE)
                     : system_page_flags_valid_[i];
        if ((block_valid & mask) != mask) {
          all_valid = false;
          break;
        }
      }
    }
    if (all_valid) {
      if (REXCVAR_GET(ht_request_range_fast_verify)) {'''),
])
