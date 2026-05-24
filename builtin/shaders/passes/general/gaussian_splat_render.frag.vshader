[vshader]
language = glsl
version = 460

[frag]
#include "include/common/color.glsl"
#include "include/common/gaussian_splat_foveated.glsl"

const float CUTOFF = 2.3539888583335364;

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 v_ScreenPos;
layout(location = 1) in vec4 v_Color;

layout(push_constant) uniform GeneralGaussianSplatRenderPushConstants
{
    vec4 foveatedGazeAndRings;
    vec4 foveatedParams;
    vec4 targetSize;
} u_PC;

bool isInsideFoveatedLayer()
{
    const uint layer = uint(u_PC.foveatedParams.w + 0.5);
    if (layer == 0u)
        return true;

    const vec2 viewportUv = (gl_FragCoord.xy - vec2(0.5)) / max(u_PC.targetSize.xy, vec2(1.0));
    const float projectionYSign = u_PC.targetSize.z == 0.0 ? 1.0 : u_PC.targetSize.z;
    const float eccentricityDegrees =
        gaussianFoveatedEccentricityDegreesFromUv(
            viewportUv,
            u_PC.foveatedGazeAndRings.xy,
            u_PC.foveatedParams.xy,
            projectionYSign);

    return gaussianFoveatedLayerContains(layer - 1u,
                                         eccentricityDegrees,
                                         u_PC.foveatedGazeAndRings.zw,
                                         u_PC.foveatedParams.z);
}

void main()
{
    if (!isInsideFoveatedLayer())
    {
        discard;
    }

    const float a = dot(v_ScreenPos, v_ScreenPos);
    if (a > 2.0 * CUTOFF)
    {
        outColor = vec4(0.0);
    }
    else
    {
        const float b = min(0.99, exp(-a) * v_Color.a);
        outColor      = vec4(sRGBToLinear(v_Color.rgb), 1.0) * b;
    }
}
