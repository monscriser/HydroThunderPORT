#!/usr/bin/env python3
"""Proof mode for B57 (ht_fetch_unchanged_skip). Apply AFTER patch_fetch_unchanged_lockless.py.

B57 leaves three "up to date" marks set when a fetch constant is rewritten with the same value.
ht_fetch_unchanged_verify=true checks, at every draw, that each mark still set equals a fresh
recomputation from the current registers:
  textures: every used texture binding in sync == BindingInfoFromFetchConstant + host swizzle now;
  vertex:   every used vertex buffer in sync == address/size of the current vertex fetch constant;
  fetch UBO: when the fetch constant buffer is not re-uploaded, the registers == the bytes uploaded.
Any mismatch means a skipped invalidation hid a real change. Counters: [ht-b57-verify].
ht_fetch_unchanged_verify_break=true (positive control) perturbs only the recomputed reference.
Usage: patch_fetch_unchanged_verify.py <sdk root>"""
import sys, os

sdk = sys.argv[1]
CP = os.path.join(sdk, "src/graphics/vulkan/command_processor.cpp")
TC = os.path.join(sdk, "src/graphics/pipeline/texture/cache.cpp")
CPH = os.path.join(sdk, "include/rex/graphics/vulkan/command_processor.h")


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


edit(TC, [
    ('REXCVAR_DEFINE_INT32(texture_cache_memory_limit_render_to_texture, 24, "GPU",',
     'REXCVAR_DEFINE_BOOL(ht_fetch_unchanged_verify, false, "GPU",\n'
     '                    "TEST ONLY (B57 proof): recompute every binding kept in sync and count differences");\n'
     'REXCVAR_DEFINE_BOOL(ht_fetch_unchanged_verify_break, false, "GPU",\n'
     '                    "TEST ONLY (B57 positive control): perturb the recomputed reference");\n'
     'namespace rex::graphics {\n'
     'uint64_t ht_b57_verify_checks[3] = {0, 0, 0};      // textures, vertex, fetch buffer\n'
     'uint64_t ht_b57_verify_mismatches[3] = {0, 0, 0};\n'
     'void HtB57VerifyLog() {\n'
     '  const uint64_t n = ht_b57_verify_checks[0] + ht_b57_verify_checks[1] + ht_b57_verify_checks[2];\n'
     '  if ((n & ((UINT64_C(1) << 20) - 1)) < 1) {\n'
     '    REXGPU_WARN("[ht-b57-verify] textures={}/{} vertex={}/{} fetch_buffer={}/{} (checked/mismatches)",\n'
     '                ht_b57_verify_checks[0], ht_b57_verify_mismatches[0], ht_b57_verify_checks[1],\n'
     '                ht_b57_verify_mismatches[1], ht_b57_verify_checks[2], ht_b57_verify_mismatches[2]);\n'
     '  }\n'
     '}\n'
     '}  // namespace rex::graphics\n'
     'REXCVAR_DEFINE_INT32(texture_cache_memory_limit_render_to_texture, 24, "GPU",'),
    ('  uint32_t textures_remaining = used_texture_mask & ~texture_bindings_in_sync_;\n',
     '''  if (REXCVAR_GET(ht_fetch_unchanged_verify)) {
    // B57 proof: a binding kept in sync must equal a fresh computation from the registers.
    uint32_t in_sync = used_texture_mask & texture_bindings_in_sync_;
    uint32_t vi;
    while (rex::bit_scan_forward(in_sync, &vi)) {
      in_sync &= ~(UINT32_C(1) << vi);
      const TextureBinding& binding = texture_bindings_[vi];
      xenos::xe_gpu_texture_fetch_t fetch = regs.GetTextureFetch(vi);
      TextureKey key;
      uint8_t signs = kSwizzledSignsUnsigned;
      BindingInfoFromFetchConstant(fetch, key, &signs);
      bool same = key.is_valid == binding.key.is_valid;
      if (same && key.is_valid) {
        uint32_t swizzle = GuestToHostSwizzle(fetch.swizzle, GetHostFormatSwizzle(key));
        if (REXCVAR_GET(ht_fetch_unchanged_verify_break)) swizzle ^= 1;
        same = key == binding.key && signs == binding.swizzled_signs &&
               swizzle == binding.host_swizzle;
      } else if (same && REXCVAR_GET(ht_fetch_unchanged_verify_break)) {
        same = false;
      }
      ++ht_b57_verify_checks[0];
      if (!same) ++ht_b57_verify_mismatches[0];
      HtB57VerifyLog();
    }
  }
  uint32_t textures_remaining = used_texture_mask & ~texture_bindings_in_sync_;
'''),
])

edit(CPH, [
    ('  bool in_render_pass_ = false;\n',
     '  bool in_render_pass_ = false;\n'
     '  // B57 proof (ht_fetch_unchanged_verify): bytes of the last fetch constant buffer upload.\n'
     '  uint32_t ht_b57_fetch_uploaded_[6 * 32] = {};\n'),
])

edit(CP, [
    ('REXCVAR_DEFINE_BOOL(ht_fetch_unchanged_skip, false, "GPU/Vulkan",',
     'REXCVAR_DECLARE(bool, ht_fetch_unchanged_verify);\n'
     'REXCVAR_DECLARE(bool, ht_fetch_unchanged_verify_break);\n'
     'namespace rex::graphics {\n'
     'extern uint64_t ht_b57_verify_checks[3];\n'
     'extern uint64_t ht_b57_verify_mismatches[3];\n'
     'void HtB57VerifyLog();\n'
     '}  // namespace rex::graphics\n'
     'REXCVAR_DEFINE_BOOL(ht_fetch_unchanged_skip, false, "GPU/Vulkan",'),
    ('''      if (vertex_buffers_in_sync_[vfetch_index >> 6] & vfetch_bit) {
        continue;
      }
      xenos::xe_gpu_vertex_fetch_t vfetch_constant = regs.GetVertexFetch(vfetch_index);
''', '''      if (vertex_buffers_in_sync_[vfetch_index >> 6] & vfetch_bit) {
        if (REXCVAR_GET(ht_fetch_unchanged_verify)) {
          // B57 proof: a vertex buffer kept in sync must match the current fetch constant.
          xenos::xe_gpu_vertex_fetch_t now = regs.GetVertexFetch(vfetch_index);
          uint32_t address = now.address + (REXCVAR_GET(ht_fetch_unchanged_verify_break) ? 1 : 0);
          const VertexBufferState& kept = vertex_buffer_states_[vfetch_index];
          ++ht_b57_verify_checks[1];
          if (kept.address != address || kept.size != now.size) ++ht_b57_verify_mismatches[1];
          HtB57VerifyLog();
        }
        continue;
      }
      xenos::xe_gpu_vertex_fetch_t vfetch_constant = regs.GetVertexFetch(vfetch_index);
'''),
    ('''    // Fetch constants.
    if (!(current_constant_buffers_up_to_date_ &
          (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch))) {''',
     '''    // Fetch constants.
    if (REXCVAR_GET(ht_fetch_unchanged_verify) &&
        (current_constant_buffers_up_to_date_ &
         (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch))) {
      // B57 proof: the buffer kept bound must hold the current registers.
      uint32_t first = ht_b57_fetch_uploaded_[0];
      if (REXCVAR_GET(ht_fetch_unchanged_verify_break)) ht_b57_fetch_uploaded_[0] ^= 1;
      ++ht_b57_verify_checks[2];
      if (std::memcmp(ht_b57_fetch_uploaded_, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],
                      sizeof(ht_b57_fetch_uploaded_))) {
        ++ht_b57_verify_mismatches[2];
      }
      ht_b57_fetch_uploaded_[0] = first;
      HtB57VerifyLog();
    }
    if (!(current_constant_buffers_up_to_date_ &
          (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch))) {'''),
    ('''      std::memcpy(mapping, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], kFetchConstantsSize);
''', '''      std::memcpy(mapping, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], kFetchConstantsSize);
      if (REXCVAR_GET(ht_fetch_unchanged_verify)) {
        std::memcpy(ht_b57_fetch_uploaded_, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],
                    kFetchConstantsSize);
      }
'''),
])
