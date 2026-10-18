// Hydro Thunder port: proof mode for ht_texture_direct (cvar
// ht_texture_direct_verify, off by default). Compares every texel written by
// the direct load with the output of the original SDK load shader (linear
// scratch buffer) and counts the differences.
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(push_constant) uniform Constants {
  uint offset_elements;
  uint pitch_elements;
  uint width;
  uint height;
} constants;
layout(std430, set = 0, binding = 0) readonly buffer Source { uint data[]; } source;
layout(set = 1, binding = 0, r32ui) uniform readonly uimage2D texture_direct;
layout(std430, set = 2, binding = 0) buffer Counters { uint mismatches; uint checked_groups; } counters;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= constants.width || p.y >= constants.height) return;
  if (gl_LocalInvocationIndex == 0u) atomicAdd(counters.checked_groups, 1u);
  uint expected = source.data[constants.offset_elements + p.y * constants.pitch_elements + p.x];
  if (imageLoad(texture_direct, ivec2(p)).x != expected) atomicAdd(counters.mismatches, 1u);
}
