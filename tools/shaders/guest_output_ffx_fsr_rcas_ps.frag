#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(push_constant, std430) uniform _1010_3305
{
    layout(offset = 16) ivec2 _m0;
    layout(offset = 24) float _m1;
} _3305;

layout(set = 0, binding = 0) uniform texture2D _3575;

layout(location = 0) out vec4 _3253;

void main()
{
    ivec2 _6990 = ivec2(uvec2(ivec2(gl_FragCoord.xy) - _3305._m0));
    vec4 _8899 = texelFetch(_3575, _6990 + ivec2(0, -1), 0);
    float _19729 = _8899.x;
    float _6930 = _8899.y;
    float _18220 = _8899.z;
    vec4 _8900 = texelFetch(_3575, _6990 + ivec2(-1, 0), 0);
    float _19730 = _8900.x;
    float _24728 = _8900.y;
    float _11386 = _8900.z;
    vec4 _19121 = texelFetch(_3575, _6990, 0);
    float _19731 = _19121.x;
    float _6931 = _19121.y;
    float _18221 = _19121.z;
    vec4 _8901 = texelFetch(_3575, _6990 + ivec2(1, 0), 0);
    float _19732 = _8901.x;
    float _6932 = _8901.y;
    float _18222 = _8901.z;
    vec4 _8902 = texelFetch(_3575, _6990 + ivec2(0, 1), 0);
    float _19733 = _8902.x;
    float _23834 = _8902.y;
    float _6945 = _8902.z;
    float _15948 = min(min(_19729, min(_19730, _19732)), _19733);
    float _15951 = min(min(_6930, min(_24728, _6932)), _23834);
    float _10905 = min(min(_18220, min(_11386, _18222)), _6945);
    float _21851 = max(max(_19729, max(_19730, _19732)), _19733);
    float _21854 = max(max(_6930, max(_24728, _6932)), _23834);
    float _10142 = max(max(_18220, max(_11386, _18222)), _6945);
    float _20919 = max(-0.1875, min(max(max(-(min(_15948, _19731) * (0.25 / _21851)), (1.0 - max(_21851, _19731)) * (1.0 / ((4.0 * _15948) + (-4.0)))), max(max(-(min(_15951, _6931) * (0.25 / _21854)), (1.0 - max(_21854, _6931)) * (1.0 / ((4.0 * _15951) + (-4.0)))), max(-(min(_10905, _18221) * (0.25 / _10142)), (1.0 - max(_10142, _18221)) * (1.0 / ((4.0 * _10905) + (-4.0)))))), 0.0)) * uintBitsToFloat(floatBitsToUint(_3305._m1));
    // htrecomp: text/HUD guard - no sharpening across very high local contrast.
    float ht_contrast = max(max(_21851 - _15948, _21854 - _15951), _10142 - _10905);
    _20919 *= 1.0 - smoothstep(0.300, 0.550, ht_contrast);
    float _16498 = (4.0 * _20919) + 1.0;
    float _9419 = uintBitsToFloat(2129764351u - floatBitsToUint(_16498));
    float _9366 = _9419 * (((-_9419) * _16498) + 2.0);
    _3253 = vec4(((_20919 * (((_19729 + _19730) + _19733) + _19732)) + _19731) * _9366, ((_20919 * (((_6930 + _24728) + _23834) + _6932)) + _6931) * _9366, ((_20919 * (((_18220 + _11386) + _6945) + _18222)) + _18221) * _9366, 1.0);
}

