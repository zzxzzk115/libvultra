[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute
USE_BASE : bool permute

[frag]
#include "include/common/gaussian_splat_foveated.glsl"

#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 outColor;

#if USE_MULTIVIEW && !PLATFORM_WEBGPU
layout(set = 3, binding = 0) uniform sampler2DArray t_FoveaLayer;
layout(set = 3, binding = 1) uniform sampler2DArray t_MidLayer;
layout(set = 3, binding = 2) uniform sampler2DArray t_OuterLayer;
#if USE_BASE
layout(set = 3, binding = 3) uniform sampler2DArray t_BaseLayer;
#endif
#else
layout(set = 3, binding = 0) uniform sampler2D t_FoveaLayer;
layout(set = 3, binding = 1) uniform sampler2D t_MidLayer;
layout(set = 3, binding = 2) uniform sampler2D t_OuterLayer;
#if USE_BASE
layout(set = 3, binding = 3) uniform sampler2D t_BaseLayer;
#endif
#endif

layout(push_constant) uniform GeneralGaussianSplatFoveatedCompositePushConstants
{
    vec4 foveatedGazeAndRings;
    vec4 foveatedParams;
} u_PC;

vec4 sampleLayer(const int layerIndex, const vec2 uv)
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    const vec3 uvw = vec3(uv, float(gl_ViewIndex));
    if (layerIndex == 0)
        return texture(t_FoveaLayer, uvw);
    if (layerIndex == 1)
        return texture(t_MidLayer, uvw);
    if (layerIndex == 2)
        return texture(t_OuterLayer, uvw);
#if USE_BASE
    return texture(t_BaseLayer, uvw);
#else
    return vec4(0.0, 0.0, 0.0, 1.0);
#endif
#else
    if (layerIndex == 0)
        return texture(t_FoveaLayer, uv);
    if (layerIndex == 1)
        return texture(t_MidLayer, uv);
    if (layerIndex == 2)
        return texture(t_OuterLayer, uv);
#if USE_BASE
    return texture(t_BaseLayer, uv);
#else
    return vec4(0.0, 0.0, 0.0, 1.0);
#endif
#endif
}

vec3 overPremultiplied(const vec4 src, const vec3 dst)
{
    return src.rgb + dst * (1.0 - clamp(src.a, 0.0, 1.0));
}

vec2 foveatedViewportUv()
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    const vec2 size = vec2(textureSize(t_FoveaLayer, 0).xy);
#else
    const vec2 size = vec2(textureSize(t_FoveaLayer, 0));
#endif
    return (gl_FragCoord.xy - vec2(0.5)) / max(size, vec2(1.0));
}

void main()
{
    vec2 sampleUv = v_TexCoord;
#if PLATFORM_WEBGPU
    sampleUv.y = 1.0 - sampleUv.y;
#endif

    const float eccentricityDegrees =
        gaussianFoveatedEccentricityDegreesFromUv(
            foveatedViewportUv(),
            u_PC.foveatedGazeAndRings.xy,
            u_PC.foveatedParams.xy,
            u_PC.foveatedParams.w == 0.0 ? 1.0 : u_PC.foveatedParams.w);
    const vec2 layerBlend =
        gaussianFoveatedRingBlend(eccentricityDegrees, u_PC.foveatedGazeAndRings.zw, u_PC.foveatedParams.z);

    const vec4 fovea = sampleLayer(0, sampleUv);
    const vec4 mid = sampleLayer(1, sampleUv);
    const vec4 outer = sampleLayer(2, sampleUv);
    const vec4 gaussian = mix(mix(fovea, mid, layerBlend.x), outer, layerBlend.y);

    vec3 color = sampleLayer(3, sampleUv).rgb;
    color = overPremultiplied(gaussian, color);
    outColor = vec4(color, 1.0);
}
