[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

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
