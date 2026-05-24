[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#include "include/common/gaussian_splat_foveated.glsl"

#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 outColor;

#if USE_MULTIVIEW && !PLATFORM_WEBGPU
layout(set = 3, binding = 0) uniform sampler2DArray t_BaseLayer;
#else
layout(set = 3, binding = 0) uniform sampler2D t_BaseLayer;
#endif

layout(push_constant) uniform GeneralGaussianSplatFoveatedDebugOverlayPushConstants
{
    vec4 foveatedGazeAndRings;
    vec4 foveatedParams;
    vec4 debugParams;
} u_PC;

vec4 sampleBase(const vec2 uv)
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    return texture(t_BaseLayer, vec3(uv, float(gl_ViewIndex)));
#else
    return texture(t_BaseLayer, uv);
#endif
}

void main()
{
    vec2 sampleUv = v_TexCoord;
#if PLATFORM_WEBGPU
    sampleUv.y = 1.0 - sampleUv.y;
#endif

    const vec2 size = max(u_PC.debugParams.xy, vec2(1.0));
    const vec2 viewportUv = (gl_FragCoord.xy - vec2(0.5)) / size;
    const float projectionYSign = u_PC.foveatedParams.w == 0.0 ? 1.0 : u_PC.foveatedParams.w;
    const float eccentricityDegrees =
        gaussianFoveatedEccentricityDegreesFromUv(
            viewportUv,
            u_PC.foveatedGazeAndRings.xy,
            u_PC.foveatedParams.xy,
            projectionYSign);

    const vec2 blend =
        gaussianFoveatedRingBlend(eccentricityDegrees, u_PC.foveatedGazeAndRings.zw, u_PC.foveatedParams.z);
    const vec3 overlay = mix(mix(vec3(1.0, 0.05, 0.05), vec3(0.0, 0.85, 0.20), blend.x),
                             vec3(0.05, 0.25, 1.0),
                             blend.y);

    const float alpha = clamp(u_PC.debugParams.z, 0.0, 1.0);
    const float dotRadius = max(u_PC.debugParams.w, 1.0);
    const vec2 pixel = gl_FragCoord.xy - vec2(0.5);
    const vec2 gazePixel = u_PC.foveatedGazeAndRings.xy * size;
    const float dotMask = 1.0 - smoothstep(dotRadius, dotRadius + 1.5, length(pixel - gazePixel));
    const float foveaLine = 1.0 - smoothstep(0.0, 0.35, abs(eccentricityDegrees - u_PC.foveatedGazeAndRings.z));
    const float midLine = 1.0 - smoothstep(0.0, 0.35, abs(eccentricityDegrees - u_PC.foveatedGazeAndRings.w));

    vec3 color = mix(sampleBase(sampleUv).rgb, overlay, alpha);
    color = mix(color, vec3(1.0), max(foveaLine, midLine) * 0.70);
    color = mix(color, vec3(1.0, 0.95, 0.0), dotMask);
    outColor = vec4(color, 1.0);
}
