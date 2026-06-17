[vshader]
id       = "builtin/general/gaussian_splat_render"
language = glsl
version = 460

[keywords]
// Union of per-stage axes: USE_MULTIVIEW drives the vertex stage, WRITE_ENTITY_ID the
// fragment stage. Each stage ignores the other's axis; both load sites pass the full set
// so the resolved variant is explicit rather than relying on unspecified-keyword defaults.
USE_MULTIVIEW   : bool permute
WRITE_ENTITY_ID : bool permute

[vert]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#endif

#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BUFFER_READONLY
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BUFFER_READONLY
#include "include/common/gpu_scene.glsl"

const float CUTOFF = 2.3539888583335364;

layout(location = 0) out vec2 v_ScreenPos;
layout(location = 1) out vec4 v_Color;

void main()
{
    const uint sortedIndex = s_GeneralGaussianSplatSortIndices.indices[uint(gl_InstanceIndex)];
    const GeneralGaussianSplatVisibleSplat splat = s_GeneralGaussianSplatVisibleSplats.splats[sortedIndex];

#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    const bool useRightEye = gl_ViewIndex != 0u;
#else
    const bool useRightEye = false;
#endif
    const uvec4 packed0 = useRightEye ? splat.packedEye1_0 : splat.packedEye0_0;
    const uvec4 packed1 = useRightEye ? splat.packedEye1_1 : splat.packedEye0_1;

    const vec2 v1 = unpackHalf2x16(packed0.x);
    const vec2 v2 = unpackHalf2x16(packed0.y);
    const vec2 centerNdc = unpackHalf2x16(packed0.z);
    const float depth = uintBitsToFloat(packed0.w);

    const vec2 colorRG = unpackHalf2x16(packed1.x);
    const vec2 colorBA = unpackHalf2x16(packed1.y);

    const float x = ((uint(gl_VertexIndex) & 1u) == 0u) ? -1.0 : 1.0;
    const float y = (uint(gl_VertexIndex) < 2u) ? 1.0 : -1.0;
    const vec2 screenPos = vec2(x, y) * CUTOFF;
    const vec2 offset = 2.0 * mat2(v1, v2) * screenPos;

    gl_Position = vec4(centerNdc + offset, clamp(depth, 0.0, 1.0), 1.0);
    v_ScreenPos = screenPos;
    v_Color = vec4(colorRG, colorBA);
}

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
        // No `discard`: the vshadersystem GLSL->WGSL lowering emits `discard; return;` which naga
        // rejects ("instructions after return"). Splats use premultiplied alpha blending
        // (src=One, dst=1-srcAlpha) with depth test/write off, so writing transparent black is
        // equivalent to discarding. (Fix the lowering in vshadersystem to restore discard.)
        outColor = vec4(0.0);
#if WRITE_ENTITY_ID
        outEntityId = vec4(0.0);
#endif
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
