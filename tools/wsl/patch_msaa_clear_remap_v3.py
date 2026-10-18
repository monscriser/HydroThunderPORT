"""Follow-up to patch_msaa_clear_remap_v2.py: scissor scaling must stay inside
the register fields. Window scissor coordinates are 14-bit unsigned and the
screen scissor 15-bit signed; 8192 already means "no scissor", and doubling
it to 16384 wrapped to 0 (an empty scissor: the clear was dropped).

usage: python3 patch_msaa_clear_remap_v3.py /root/ht/sdk
"""
import sys
from pathlib import Path

cp = Path(sys.argv[1]) / "src/graphics/vulkan/command_processor.cpp"
s = cp.read_text()
old = """      auto scale_xy = [](uint32_t value) {
        const uint32_t x = std::min((value & 0x7FFF) << 1, uint32_t(0x7FFF));
        const uint32_t y = std::min(((value >> 16) & 0x7FFF) << 1, uint32_t(0x7FFF));
        return (value & 0x80008000) | x | (y << 16);
      };
      for (uint32_t reg : {XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR,
                           XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR}) {
        ht_restore.Set(reg, scale_xy(regs[reg]));
      }"""
new = """      // Doubled and clamped to 8192, which already means "no scissor" (the
      // window fields are 14-bit unsigned, the screen ones 15-bit signed).
      auto scale_window = [](uint32_t value) {
        const uint32_t x = std::min((value & 0x3FFF) << 1, uint32_t(0x2000));
        const uint32_t y = std::min(((value >> 16) & 0x3FFF) << 1, uint32_t(0x2000));
        return (value & 0xC000C000) | x | (y << 16);
      };
      auto scale_screen = [](uint32_t value) {
        auto axis = [](uint32_t bits) {
          int32_t v = int32_t(bits << 17) >> 17;  // sign-extend 15 bits
          v = std::clamp(v * 2, -8192, 8192);
          return uint32_t(v) & 0x7FFF;
        };
        return (value & 0x80008000) | axis(value & 0x7FFF) | (axis((value >> 16) & 0x7FFF) << 16);
      };
      for (uint32_t reg : {XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR}) {
        ht_restore.Set(reg, scale_window(regs[reg]));
      }
      for (uint32_t reg : {XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR}) {
        ht_restore.Set(reg, scale_screen(regs[reg]));
      }"""
if "scale_window" not in s:
    assert old in s
    cp.write_text(s.replace(old, new, 1))
print("patched")
