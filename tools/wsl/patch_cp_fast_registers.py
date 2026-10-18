"""9.0.0 CPU: faster register writes on the command processor thread.

ht_cp_fast_registers (default true):
  1. PM4 type-0 packets that write a run of consecutive registers use the existing bulk
     path (WriteRegisterRangeFromRing -> WriteRegistersFromMem), as Xenia canary does,
     instead of one virtual WriteRegister call per register. Inside the shader constant
     ranges the Vulkan bulk path copies and sets the same dirty bits; elsewhere it falls
     back to the per-register loop, so special registers behave exactly as before.
  2. WriteRegister looks up the register table only when GPU debug logging is enabled
     (the lookup only fed a debug message).
  3. IssueDraw checks the readback-memexport cvar (mutex + string + hash lookup) only
     when the draw has memexport ranges (same && operands, cheaper one first).
ht_cp_fast_registers_verify (TEST ONLY): for type-0 runs inside the constant ranges, runs
  the bulk path, records registers + dirty bits, restores, replays the original
  per-register path and compares ([ht-cp-fast] ... compared= mismatches=). The original
  path's result is kept.
ht_cp_fast_registers_verify_break (TEST ONLY): flips one bit of the reference copy only.

usage: python3 patch_cp_fast_registers.py /root/ht/sdk   (on the 8.9.3 cumulative SDK)
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])


def sub(path, old, new, count=1):
    p = root / path
    s = p.read_text()
    assert s.count(old) == count, (path, old[:80], s.count(old))
    p.write_text(s.replace(old, new))


# --- cvars ---------------------------------------------------------------
sub("include/rex/graphics/flags.h", "REXCVAR_DECLARE(bool, vsync);\n",
    "REXCVAR_DECLARE(bool, vsync);\n"
    "REXCVAR_DECLARE(bool, ht_cp_fast_registers);\n"
    "REXCVAR_DECLARE(bool, ht_cp_fast_registers_verify);\n"
    "REXCVAR_DECLARE(bool, ht_cp_fast_registers_verify_break);\n")
sub("src/graphics/command_processor.cpp",
    'REXCVAR_DEFINE_BOOL(vsync, true, "GPU", "Enable vertical sync");\n',
    'REXCVAR_DEFINE_BOOL(vsync, true, "GPU", "Enable vertical sync");\n'
    'REXCVAR_DEFINE_BOOL(ht_cp_fast_registers, true, "GPU",\n'
    '                    "CPU: bulk type-0 register writes and cheaper per-register checks");\n'
    'REXCVAR_DEFINE_BOOL(ht_cp_fast_registers_verify, false, "GPU",\n'
    '                    "TEST ONLY: replay type-0 constant runs per register and compare state");\n'
    'REXCVAR_DEFINE_BOOL(ht_cp_fast_registers_verify_break, false, "GPU",\n'
    '                    "TEST ONLY: perturb the reference copy of the verifier");\n')

# --- base: type-0 bulk + register-table lookup only for debug logging ------
sub("include/rex/graphics/command_processor.h",
    """  virtual void WriteRegisterRangeFromRing(memory::RingBuffer* ring, uint32_t base,
                                          uint32_t num_registers);
""",
    """  virtual void WriteRegisterRangeFromRing(memory::RingBuffer* ring, uint32_t base,
                                          uint32_t num_registers);
  // ht_cp_fast_registers: a type-0 run of consecutive registers (backends may verify it).
  virtual void HtWriteType0Range(memory::RingBuffer* ring, uint32_t base,
                                 uint32_t num_registers) {
    WriteRegisterRangeFromRing(ring, base, num_registers);
  }
""")
sub("src/graphics/command_processor.cpp",
    """  const_cast<volatile uint32_t&>(regs.values[index]) = value;
  if (!regs.GetRegisterInfo(index)) {
    REXGPU_DEBUG("GPU: Write to unknown register ({:04X} = {:08X})", index, value);
  }
""",
    """  const_cast<volatile uint32_t&>(regs.values[index]) = value;
  // The register table lookup only feeds a debug message (ht_cp_fast_registers).
  bool ht_check_register_info = !REXCVAR_GET(ht_cp_fast_registers);
  if (!ht_check_register_info) {
    auto* ht_gpu_log = ::rex::GetLoggerRaw(::rex::log::gpu());
    ht_check_register_info = ht_gpu_log && ht_gpu_log->should_log(spdlog::level::debug);
  }
  if (ht_check_register_info && !regs.GetRegisterInfo(index)) {
    REXGPU_DEBUG("GPU: Write to unknown register ({:04X} = {:08X})", index, value);
  }
""")
sub("src/graphics/command_processor.cpp",
    """  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  for (uint32_t m = 0; m < count; m++) {""",
    """  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  if (!write_one_reg && count > 1 && REXCVAR_GET(ht_cp_fast_registers)) {
    HtWriteType0Range(reader, base_index, count);
    return true;
  }
  for (uint32_t m = 0; m < count; m++) {""")

# --- texture cache: state accessors for the verifier -----------------------
sub("include/rex/graphics/pipeline/texture/cache.h",
    "  void TextureFetchConstantWritten(uint32_t index) { TextureFetchConstantsWritten(index, index); }\n",
    "  uint32_t HtTextureBindingsInSync() const { return texture_bindings_in_sync_; }\n"
    "  void HtSetTextureBindingsInSync(uint32_t v) { texture_bindings_in_sync_ = v; }\n"
    "  void TextureFetchConstantWritten(uint32_t index) { TextureFetchConstantsWritten(index, index); }\n")

# --- Vulkan: verifier + memexport check order ------------------------------
sub("include/rex/graphics/vulkan/command_processor.h",
    "  void WriteRegistersFromMem(uint32_t start_index, uint32_t* base, uint32_t num_registers) override;\n",
    "  void WriteRegistersFromMem(uint32_t start_index, uint32_t* base, uint32_t num_registers) override;\n"
    "  void HtWriteType0Range(memory::RingBuffer* ring, uint32_t base,\n"
    "                         uint32_t num_registers) override;\n"
    "  uint64_t ht_cpfr_packets_ = 0, ht_cpfr_registers_ = 0, ht_cpfr_compared_ = 0,\n"
    "           ht_cpfr_mismatches_ = 0;\n")
sub("src/graphics/vulkan/command_processor.cpp",
    """void VulkanCommandProcessor::WriteRegistersFromMem(uint32_t start_index, uint32_t* base,
                                                   uint32_t num_registers) {""",
    """void VulkanCommandProcessor::HtWriteType0Range(memory::RingBuffer* ring, uint32_t base,
                                               uint32_t num_registers) {
  ++ht_cpfr_packets_;
  ht_cpfr_registers_ += num_registers;
  uint32_t end = base + num_registers - 1;
  bool constants =
      (base >= XE_GPU_REG_SHADER_CONSTANT_000_X && end <= XE_GPU_REG_SHADER_CONSTANT_511_W) ||
      (base >= XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 && end <= XE_GPU_REG_SHADER_CONSTANT_LOOP_31) ||
      (base >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 && end <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5);
  if (!REXCVAR_GET(ht_cp_fast_registers_verify) || !constants) {
    // Outside the constant ranges the bulk path is the per-register loop itself.
    CommandProcessor::HtWriteType0Range(ring, base, num_registers);
  } else {
    uint32_t* values = register_file_->values + base;
    std::vector<uint32_t> before(values, values + num_registers);
    uint32_t up0 = current_constant_buffers_up_to_date_;
    uint64_t vb0[2] = {vertex_buffers_in_sync_[0], vertex_buffers_in_sync_[1]};
    uint32_t tb0 = texture_cache_ ? texture_cache_->HtTextureBindingsInSync() : 0;
    CommandProcessor::HtWriteType0Range(ring, base, num_registers);
    std::vector<uint32_t> fast(values, values + num_registers);
    uint32_t up_fast = current_constant_buffers_up_to_date_;
    uint64_t vb_fast[2] = {vertex_buffers_in_sync_[0], vertex_buffers_in_sync_[1]};
    uint32_t tb_fast = texture_cache_ ? texture_cache_->HtTextureBindingsInSync() : 0;
    // Restore and replay the original per-register path; its result is kept.
    std::memcpy(values, before.data(), num_registers * sizeof(uint32_t));
    current_constant_buffers_up_to_date_ = up0;
    vertex_buffers_in_sync_[0] = vb0[0];
    vertex_buffers_in_sync_[1] = vb0[1];
    if (texture_cache_) texture_cache_->HtSetTextureBindingsInSync(tb0);
    for (uint32_t i = 0; i < num_registers; ++i) {
      WriteRegister(base + i, fast[i]);
    }
    std::vector<uint32_t> ref(values, values + num_registers);
    if (REXCVAR_GET(ht_cp_fast_registers_verify_break)) ref[0] ^= 1;
    uint32_t tb_ref = texture_cache_ ? texture_cache_->HtTextureBindingsInSync() : 0;
    ++ht_cpfr_compared_;
    if (ref != fast || current_constant_buffers_up_to_date_ != up_fast ||
        vertex_buffers_in_sync_[0] != vb_fast[0] || vertex_buffers_in_sync_[1] != vb_fast[1] ||
        tb_ref != tb_fast) {
      ++ht_cpfr_mismatches_;
    }
  }
  if ((ht_cpfr_packets_ & ((UINT64_C(1) << 18) - 1)) == 0) {
    REXGPU_WARN("[ht-cp-fast] packets={} registers={} compared={} mismatches={}", ht_cpfr_packets_,
                ht_cpfr_registers_, ht_cpfr_compared_, ht_cpfr_mismatches_);
  }
}

void VulkanCommandProcessor::WriteRegistersFromMem(uint32_t start_index, uint32_t* base,
                                                   uint32_t num_registers) {""")
sub("src/graphics/vulkan/command_processor.cpp",
    """  if (IsReadbackMemexportEnabled(REXCVAR_GET(vulkan_readback_memexport)) &&
      !memexport_ranges_.empty()) {""",
    """  // ht_cp_fast_registers: the cvar lookup (mutex + string + hash) only when needed.
  if ((REXCVAR_GET(ht_cp_fast_registers)
           ? (!memexport_ranges_.empty() &&
              IsReadbackMemexportEnabled(REXCVAR_GET(vulkan_readback_memexport)))
           : (IsReadbackMemexportEnabled(REXCVAR_GET(vulkan_readback_memexport)) &&
              !memexport_ranges_.empty()))) {""")
print("patched cp fast registers")
