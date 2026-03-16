[vshader]
language = glsl
version = 460

[keywords]
SPLAT_OUTPUT_SRGB : bool permute

[frag]
#ifndef SPLAT_OUTPUT_SRGB
#define SPLAT_OUTPUT_SRGB 0
#endif

layout(location = 0) in vec2 v_FragPos;
layout(location = 1) flat in uint v_SplatIndex;
layout(location = 2) in vec4 v_SplatColor;
layout(location = 0) out vec4 FragColor;

vec3 hashColor(uint id)
{
    uint n = id * 1664525u + 1013904223u;
    return vec3((n & 0xFFu), (n >> 8) & 0xFFu, (n >> 16) & 0xFFu) / 255.0;
}

vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    bvec3 cutoff = lessThanEqual(c, vec3(0.0031308));
    return vec3(cutoff.x ? lo.x : hi.x, cutoff.y ? lo.y : hi.y, cutoff.z ? lo.z : hi.z);
}

void main()
{
    const float kOpacityDiscardThreshold = 1.0 / 512.0;

    float A = dot(v_FragPos, v_FragPos);
    if (A > 8.0)
        discard;

    float alpha = exp(-0.5 * A) * v_SplatColor.a;
    if (alpha < kOpacityDiscardThreshold)
        discard;

    vec3 color = (v_SplatColor.a > 0.0) ? v_SplatColor.rgb : hashColor(v_SplatIndex);

#if SPLAT_OUTPUT_SRGB
    color = linearToSrgb(color);
#endif

    FragColor = vec4(color * alpha, alpha);
}
