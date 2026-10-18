// Hydro Thunder port: texture upload blit. The texture load shader leaves the
// converted texels in a linear scratch buffer; copying that buffer straight
// into the image (vkCmdCopyBufferToImage) is very slow on NVIDIA. This shader
// writes the same bytes into an integer staging image instead, which is then
// moved with vkCmdCopyImage. Idea from Xenia Edge commit a766c62 (has207).
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(push_constant) uniform Constants {
  uint offset_elements;
  uint pitch_elements;
  uint width;
  uint height;
} constants;layout(std430, set = 0, binding = 0) readonly buffer Source { uvec2 data[]; } source;
// rgba16ui keeps the 8 bytes in the same order without the extended-format
// capability that rg32ui would need.
layout(set = 1, binding = 0, rgba16ui) uniform writeonly uimage2D dest;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= constants.width || p.y >= constants.height) return;
  uvec2 v = source.data[constants.offset_elements + p.y * constants.pitch_elements + p.x];
  imageStore(dest, ivec2(p), uvec4(v.x & 0xFFFFu, v.x >> 16u, v.y & 0xFFFFu, v.y >> 16u));
}