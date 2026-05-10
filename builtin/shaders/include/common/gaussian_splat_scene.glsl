#ifndef VULTRA_GAUSSIAN_SPLAT_SCENE_GLSL
#define VULTRA_GAUSSIAN_SPLAT_SCENE_GLSL

#ifndef VULTRA_SCENE_SET
#define VULTRA_SCENE_SET 0
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_DRAW_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_DRAW_BINDING 13
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_PACKED_SOURCE_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_PACKED_SOURCE_BINDING 14
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BINDING 15
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BINDING 16
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BINDING 17
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BINDING 18
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_DISPATCH_ARGS_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_DISPATCH_ARGS_BINDING 19
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_INDIRECT_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_INDIRECT_BINDING 20
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_STORAGE_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_STORAGE_BINDING 21
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_SH_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_SH_BINDING 22
#endif

#ifndef VULTRA_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BINDING
#define VULTRA_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BINDING 27
#endif

const uint GENERAL_GAUSSIAN_SPLAT_SELECTED_FLAG_TRANSITION = 2u;
const uint GENERAL_GAUSSIAN_SPLAT_SELECTED_FLAG_INVALID = 0x80000000u;

struct GeneralGaussianSplatDrawRecord
{
    uint splatIndex;
    uint pointOffset;
    uint pointCount;
    uint shDegree;
    vec4 params0;
    mat4 model;
};

struct GeneralGaussianSplatPackedSource
{
    uvec4 posOpacity;
    uvec4 covariance0;
    uvec4 colorSh0;
    uvec4 aux0;
};

struct GeneralGaussianSplatSelectedSource
{
    uint sourceIndex;
    uint drawIndex;
    uint packedWeight;
    uint flags;
};

struct GeneralGaussianSplatVisibleSplat
{
    uvec4 packedEye0_0; // x=basis0.xy, y=basis1.xy, z=centerNdc.xy, w=floatBits(depth)
    uvec4 packedEye0_1; // x=color.rg, y=color.ba, z=packedSourceIndex, w=drawIndex
    uvec4 packedEye1_0; // x=basis0.xy, y=basis1.xy, z=centerNdc.xy, w=floatBits(depth)
    uvec4 packedEye1_1; // x=color.rg, y=color.ba, z=packedSourceIndex, w=drawIndex
};

struct GeneralGaussianSplatDispatchArgs
{
    uint dispatchX;
    uint dispatchY;
    uint dispatchZ;
    uint reserved;
};

struct GeneralGaussianSplatIndirectCommand
{
    uint count;
    uint instanceCount;
    uint first;
    uint firstInstance;
};

vec3 decodeGeneralGaussianSplatPosition(const GeneralGaussianSplatPackedSource src)
{
    return vec3(uintBitsToFloat(src.posOpacity.x), uintBitsToFloat(src.posOpacity.y), uintBitsToFloat(src.posOpacity.z));
}

mat3 decodeGeneralGaussianSplatCovariance(const GeneralGaussianSplatPackedSource src)
{
    const vec2 p0 = unpackHalf2x16(src.covariance0.x);
    const vec2 p1 = unpackHalf2x16(src.covariance0.y);
    const vec2 p2 = unpackHalf2x16(src.covariance0.z);

    mat3 sigma = mat3(0.0);
    sigma[0][0] = p0.x;
    sigma[1][0] = p0.y;
    sigma[0][1] = p0.y;
    sigma[2][0] = p1.x;
    sigma[0][2] = p1.x;
    sigma[1][1] = p1.y;
    sigma[2][1] = p2.x;
    sigma[1][2] = p2.x;
    sigma[2][2] = p2.y;
    return sigma;
}

vec4 decodeGeneralGaussianSplatBaseColorOpacity(const GeneralGaussianSplatPackedSource src)
{
    const vec2 rg = unpackHalf2x16(src.colorSh0.x);
    const vec2 ba = unpackHalf2x16(src.colorSh0.y);
    return vec4(rg.x, rg.y, ba.x, ba.y);
}

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_DRAW_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_DRAW_BINDING, std430) readonly buffer
    GeneralGaussianSplatDrawBuffer
{
    GeneralGaussianSplatDrawRecord draws[];
} s_GeneralGaussianSplatDraws;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_PACKED_SOURCE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_PACKED_SOURCE_BINDING, std430) readonly buffer
    GeneralGaussianSplatPackedSourceBuffer
{
    GeneralGaussianSplatPackedSource points[];
} s_GeneralGaussianSplatPackedSources;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BINDING, std430) readonly buffer
    GeneralGaussianSplatSelectedSourceBuffer
{
    GeneralGaussianSplatSelectedSource sources[];
} s_GeneralGaussianSplatSelectedSources;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BUFFER_READWRITE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BINDING, std430) buffer
    GeneralGaussianSplatSelectedSourceBuffer
{
    GeneralGaussianSplatSelectedSource sources[];
} s_GeneralGaussianSplatSelectedSources;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BUFFER_READONLY
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BINDING, std430) readonly buffer
    GeneralGaussianSplatVisibleSplatBuffer
{
    GeneralGaussianSplatVisibleSplat splats[];
} s_GeneralGaussianSplatVisibleSplats;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BUFFER_READWRITE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BINDING, std430) buffer
    GeneralGaussianSplatVisibleSplatBuffer
{
    GeneralGaussianSplatVisibleSplat splats[];
} s_GeneralGaussianSplatVisibleSplats;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BUFFER_READONLY
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BINDING, std430) readonly buffer
    GeneralGaussianSplatSortKeyBuffer
{
    uint keys[];
} s_GeneralGaussianSplatSortKeys;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BUFFER_READWRITE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BINDING, std430) buffer
    GeneralGaussianSplatSortKeyBuffer
{
    uint keys[];
} s_GeneralGaussianSplatSortKeys;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BUFFER_READONLY
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BINDING, std430) readonly buffer
    GeneralGaussianSplatSortIndexBuffer
{
    uint indices[];
} s_GeneralGaussianSplatSortIndices;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BUFFER_READWRITE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BINDING, std430) buffer
    GeneralGaussianSplatSortIndexBuffer
{
    uint indices[];
} s_GeneralGaussianSplatSortIndices;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BINDING, std430) buffer
    GeneralGaussianSplatVisibleCountBuffer
{
    uint visibleCount;
} s_GeneralGaussianSplatVisibleCount;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_DISPATCH_ARGS_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_DISPATCH_ARGS_BINDING, std430) buffer
    GeneralGaussianSplatDispatchArgsBuffer
{
    GeneralGaussianSplatDispatchArgs dispatchArgs;
} s_GeneralGaussianSplatDispatchArgs;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_INDIRECT_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_INDIRECT_BINDING, std430) buffer
    GeneralGaussianSplatIndirectBuffer
{
    GeneralGaussianSplatIndirectCommand command;
} s_GeneralGaussianSplatIndirect;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_STORAGE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_SORT_STORAGE_BINDING, std430) buffer
    GeneralGaussianSplatSortStorageBuffer
{
    uint words[];
} s_GeneralGaussianSplatSortStorage;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SH_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_GENERAL_GAUSSIAN_SPLAT_SH_BINDING, std430) readonly buffer
    GeneralGaussianSplatShBuffer
{
    uvec2 coeffs[];
} s_GeneralGaussianSplatSh;
#endif

#ifdef VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SH_BUFFER
vec3 decodeGeneralGaussianSplatShCoeff(const uint coeffOffset)
{
    const uvec2 packedCoeff = s_GeneralGaussianSplatSh.coeffs[coeffOffset];
    const vec2 rg = unpackHalf2x16(packedCoeff.x);
    const vec2 bz = unpackHalf2x16(packedCoeff.y);
    return vec3(rg, bz.x);
}
#endif

#endif
