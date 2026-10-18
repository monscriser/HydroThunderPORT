"""MEASUREMENT ONLY: trace the occlusion (ZPD) events of the HT_TRACE_FRAME frames.

  [ht-zpd] draw=<draws so far in the traced frame> addr=<report address> end=<0|1> value=<counter written>
  [ht-zpd-close] host=<host query> key=<slot> est=<estimate used>
  [ht-zpd-res] host=<host query> key=<slot> samples=<measured> est=<estimate it replaced>
Apply on top of patch_draw_timing.py + patch_barrier_trace.py (they add the per-draw
lines with VS/PS hashes, so the draws inside each query can be identified).
usage: python3 -I patch_zpd_trace.py /root/ht/sdk
"""
import sys
from pathlib import Path

p = Path(sys.argv[1]) / 'src/graphics/vulkan/command_processor.cpp'
s = p.read_text()

def rep(old, new):
    global s
    assert s.count(old) == 1, old[:80]
    s = s.replace(old, new)

rep('void VulkanCommandProcessor::ZpdCloseInterval(uint32_t key) {\n',
    'namespace {\nstd::unordered_set<uint32_t> ht_zpd_traced_hosts;\n}  // namespace\n\n'
    'void VulkanCommandProcessor::ZpdCloseInterval(uint32_t key) {\n')
rep('''  WriteGuestOcclusionResult(report_address, zpd_counter_estimate_ & 0xFFFFFFFFull);
  if (is_end && REXCVAR_GET(ht_zpd_flush)) {''',
    '''  WriteGuestOcclusionResult(report_address, zpd_counter_estimate_ & 0xFFFFFFFFull);
  if (ht_trace_active) {
    REXGPU_WARN("[ht-zpd] draw={} addr={:08X} end={} value={}", uint64_t(ht_trace_draw),
                report_address, is_end ? 1 : 0, zpd_counter_estimate_ & 0xFFFFFFFFull);
  }
  if (is_end && REXCVAR_GET(ht_zpd_flush)) {''')
rep('''  zpd_counter_estimate_ += estimate;
  zpd_intervals_.push_back({closed_index, GetCurrentSubmission(), key, estimate});''',
    '''  zpd_counter_estimate_ += estimate;
  if (ht_trace_active) {
    ht_zpd_traced_hosts.insert(closed_index);
    REXGPU_WARN("[ht-zpd-close] host={} key={:03X} est={}", closed_index, key, estimate);
  }
  zpd_intervals_.push_back({closed_index, GetCurrentSubmission(), key, estimate});''')
rep('''    const uint64_t samples = NormalizeOcclusionSamples(results[interval.host_index]);''',
    '''    const uint64_t samples = NormalizeOcclusionSamples(results[interval.host_index]);
    {
      // Estimate quality over every keyed interval: what the game was told (estimate)
      // against what the GPU measured. leak = told visible, measured hidden; miss = the
      // opposite. Logged every 4000 keyed intervals.
      static uint64_t n = 0, leak = 0, miss = 0, abs_err = 0, meas = 0;
      if (interval.key != UINT32_MAX) {
        ++n;
        leak += interval.estimate > 0 && samples == 0;
        miss += interval.estimate == 0 && samples > 0;
        abs_err += interval.estimate > samples ? interval.estimate - samples : samples - interval.estimate;
        meas += samples;
        if (n % 4000 == 0) {
          REXGPU_WARN("[ht-zpd-stats] intervals={} leak={} miss={} abs_err={} measured={}", n, leak, miss,
                      abs_err, meas);
        }
      }
    }
    if (ht_zpd_traced_hosts.erase(interval.host_index)) {
      REXGPU_WARN("[ht-zpd-res] host={} key={:03X} samples={} est={}", interval.host_index,
                  interval.key, samples, interval.estimate);
    }''')
# Depth/stencil state of the occlusion test draws (depth-only sphere, VS 72F241126568C627)
# and of every other draw inside the traced frame that has no pixel shader.
rep('''  reg::RB_DEPTHCONTROL normalized_depth_control = draw_util::GetNormalizedDepthControl(regs);
''', '''  reg::RB_DEPTHCONTROL normalized_depth_control = draw_util::GetNormalizedDepthControl(regs);
  if (ht_trace_active && !pixel_shader) {
    REXGPU_WARN("[ht-zpd-draw] draw={} vs={:016X} depth_raw={:08X} depth_norm={:08X} stencil={:08X} "
                "mode={} color_mask={:08X} depth_info={:08X} cull={:08X}",
                uint64_t(ht_trace_draw), vertex_shader->ucode_data_hash(),
                regs.Get<reg::RB_DEPTHCONTROL>().value, normalized_depth_control.value,
                regs[XE_GPU_REG_RB_STENCILREFMASK], uint32_t(regs.Get<reg::RB_MODECONTROL>().edram_mode),
                regs[XE_GPU_REG_RB_COLOR_MASK], regs[XE_GPU_REG_RB_DEPTH_INFO],
                regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL]);
  }
''')
p.write_text(s)
print('patched ZPD trace (measurement only)')
