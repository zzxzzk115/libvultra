[vshader]
language = glsl
version = 460

[keywords]
WRITE_ENTITY_ID : bool permute

[frag]
#include "include/common/color.glsl"
#include "include/common/gaussian_splat_foveated.glsl"
const float CUTOFF = 2.3539888583335364;

layout(location = 0) out vec4 outColor;
#if WRITE_ENTITY_ID
layout(location = 1) out vec4 outEntityId;
#endif
layout(location = 0) in vec2 v_ScreenPos;
layout(location = 1) in vec4 v_Color;

layout(set = 1, binding = 30) uniform GeneralGaussianSplatRenderUniforms
{
    vec4 foveatedGazeAndRings;
    vec4 foveatedParams;
    vec4 targetSize;
    uvec4 entityInfo;
} u_PC;

bool isInsideFoveatedLayer()
{
    const uint layer = uint(u_PC.foveatedParams.w + 0.5);
    if (layer == 0u)
        return true;

    const vec2 viewportUv = (gl_FragCoord.xy - vec2(0.5)) / max(u_PC.targetSize.xy, vec2(1.0));
    const float eccentricityDegrees =
        gaussianFoveatedEccentricityDegreesFromUv(viewportUv, u_PC.foveatedGazeAndRings.xy, u_PC.foveatedParams.xy);
    return gaussianFoveatedLayerContains(layer - 1u,
                                         eccentricityDegrees,
                                         u_PC.foveatedGazeAndRings.zw,
                                         u_PC.foveatedParams.z);
}

void main()
{
    const float a = dot(v_ScreenPos, v_ScreenPos);
    if (!isInsideFoveatedLayer() || a > 2.0 * CUTOFF)
    {
        discard;
    }
    else
    {
        const float b = min(0.99, exp(-a) * v_Color.a);
        outColor      = vec4(sRGBToLinear(v_Color.rgb), 1.0) * b;
#if WRITE_ENTITY_ID
        const uint id = u_PC.entityInfo.x;
        outEntityId = vec4(float(id & 0xFFu),
                           float((id >> 8u) & 0xFFu),
                           float((id >> 16u) & 0xFFu),
                           255.0) /
                      255.0;
#endif
    }
}
