// Hydro Thunder port: proof mode for ht_direct_resolve (cvar
// ht_direct_resolve_verify, off by default). Compares the destination range
// written by the direct resolve with the snapshot written by the original
// dump + resolve copy, word by word, and counts the differences.
#version 450
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform Constants { uint word_count; } constants;
layout(std430, set = 0, binding = 0) readonly buffer Direct { uint data[]; } direct;
layout(std430, set = 1, binding = 0) readonly buffer Reference { uint data[]; } reference;
layout(std430, set = 2, binding = 0) buffer Counters { uint mismatches; uint checked_groups; } counters;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= constants.word_count) return;
  if (gl_LocalInvocationIndex == 0u) atomicAdd(counters.checked_groups, 1u);
  if (direct.data[i] != reference.data[i]) atomicAdd(counters.mismatches, 1u);
}
