"""MEASUREMENT ONLY: UpdateBindings CPU breakdown; revert before final builds."""
from pathlib import Path
import sys
p=Path(sys.argv[1])/'src/graphics/vulkan/command_processor.cpp'
s=p.read_text()
anchor='namespace rex::graphics::vulkan {'
assert s.count(anchor)==1
s=s.replace(anchor,anchor+'\nnamespace {\nuint64_t ht_bind_ticks[5] = {}, ht_bind_calls = 0, ht_bind_sets = 0, ht_bind_writes = 0;\n}\n',1)
start=s.index('bool VulkanCommandProcessor::UpdateBindings(')
end=s.index('uint32_t VulkanCommandProcessor::WriteTransientTextureBindings(',start)
f=s[start:end]
f=f.replace('  const RegisterFile& regs = *register_file_;','''  uint64_t ht_bt = PerfTicks();
  ++ht_bind_calls;
  auto ht_bmark = [&](size_t stage) {
    const uint64_t now = PerfTicks(); ht_bind_ticks[stage] += now - ht_bt; ht_bt = now;
  };
  const RegisterFile& regs = *register_file_;''',1)
for anchor,stage in [('  // Textures and samplers.',0),('  // Write the new descriptor sets.',1),('  // Write.\n',2),('  // Bind the new descriptor sets.',3),('  return true;\n}',4)]:
    assert f.count(anchor)==1,(anchor,f.count(anchor))
    f=f.replace(anchor,f'  ht_bmark({stage});\n'+anchor)
f=f.replace('    VkDescriptorSet constants_descriptor_set;','    ++ht_bind_sets;\n    VkDescriptorSet constants_descriptor_set;',1)
f=f.replace('  // Write.\n','  ht_bind_writes += write_descriptor_set_count;\n  // Write.\n',1)
s=s[:start]+f+s[end:]
anchor='      perf_cpu_ticks_.fill(0);'
assert s.count(anchor)==1
s=s.replace(anchor,'''      {
        auto bms = [&](size_t stage) { return double(ht_bind_ticks[stage]) * ns_per_tick / 1e6 / f; };
        REXGPU_WARN("[ht-bindings-detail] constants={:.4f} images={:.4f} alloc={:.4f} update={:.4f} bind={:.4f} calls/frame={:.1f} sets/frame={:.1f} writes/frame={:.1f}",
            bms(0),bms(1),bms(2),bms(3),bms(4),double(ht_bind_calls)/f,double(ht_bind_sets)/f,double(ht_bind_writes)/f);
        for (auto& ticks : ht_bind_ticks) ticks=0;
        ht_bind_calls=ht_bind_sets=ht_bind_writes=0;
      }
'''+anchor)
p.write_text(s)
print('UpdateBindings measurement instrumentation applied (REVERT AFTER)')
