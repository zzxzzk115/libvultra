[vshader]
language = glsl
version = 460

[keywords]
NEED_SURFACE_INFO : bool permute
USE_DEPTH_TRANSMITTANCE : bool permute
USE_FRAGMENT_INTERLOCK : bool permute

[frag]
#include "include/common/color.glsl"

#ifndef NEED_SURFACE_INFO
#define NEED_SURFACE_INFO 0
#endif

#ifndef USE_DEPTH_TRANSMITTANCE
#define USE_DEPTH_TRANSMITTANCE 0
#endif

#ifndef USE_FRAGMENT_INTERLOCK
#define USE_FRAGMENT_INTERLOCK 0
#endif

#if USE_FRAGMENT_INTERLOCK
#extension GL_ARB_fragment_shader_interlock : require
layout(pixel_interlock_ordered) in;
#endif

layout(location = 0) in vec2 v_FragPos;
layout(location = 1) flat in uint v_SplatIndex;
layout(location = 2) in vec4 v_SplatColor;
layout(location = 0) out vec4 FragColor;
#if NEED_SURFACE_INFO && !USE_DEPTH_TRANSMITTANCE
layout(location = 1) out vec4 FragDepthAccum;
#endif
#if NEED_SURFACE_INFO && USE_DEPTH_TRANSMITTANCE
layout(push_constant) uniform GaussianSplatPushConstants
{
    float frustumDilation;
    float alphaCullThreshold;
    float sizeCullingMinPixels;
    float splatScale;
    float maxAxisPixels;
    float depthIsoThreshold;
} u_PC;

layout(set = 0, binding = 22, rg32f) uniform coherent image2D u_DepthTransmittance;
#endif

vec3 hashColor(uint id)
{
    uint n = id * 1664525u + 1013904223u;
    return vec3((n & 0xFFu), (n >> 8) & 0xFFu, (n >> 16) & 0xFFu) / 255.0;
}

void main()
{
    const float kOpacityDiscardThreshold = 1.0 / 255.0;

    float A = dot(v_FragPos, v_FragPos);
    if (A > 8.0)
        discard;

    float alpha = exp(-0.5 * A) * v_SplatColor.a;
    if (alpha < kOpacityDiscardThreshold)
        discard;

    vec3 color = (v_SplatColor.a > 0.0) ? v_SplatColor.rgb : hashColor(v_SplatIndex);

    color = sRGBToLinear(color);

    FragColor = vec4(color * alpha, alpha);
#if NEED_SURFACE_INFO && USE_DEPTH_TRANSMITTANCE
    ivec2 pixelCoord = ivec2(gl_FragCoord.xy);

#if USE_FRAGMENT_INTERLOCK
    beginInvocationInterlockARB();
#endif

    vec4 current = imageLoad(u_DepthTransmittance, pixelCoord);
    float pickedDepth = current.r;
    float previousTransmittance = current.g;
    float transmittance = previousTransmittance * (1.0 - alpha);
    if (pickedDepth == 0.0 && transmittance < u_PC.depthIsoThreshold)
        pickedDepth = gl_FragCoord.z;

    imageStore(u_DepthTransmittance, pixelCoord, vec4(pickedDepth, transmittance, 0.0, 0.0));

#if USE_FRAGMENT_INTERLOCK
    endInvocationInterlockARB();
#endif
#elif NEED_SURFACE_INFO
    FragDepthAccum = vec4(gl_FragCoord.z * alpha, 0.0, 0.0, alpha);
#endif
}
