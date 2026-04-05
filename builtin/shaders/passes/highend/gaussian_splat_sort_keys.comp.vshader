[vshader]
language = glsl
version = 460

[keywords]
USE_SCENE_DEPTH : bool permute

[comp]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_SPLAT_CENTER_BUFFER
#define VULTRA_DECLARE_SPLAT_COVARIANCE_BUFFER
#define VULTRA_DECLARE_SPLAT_COLOR_BUFFER
#define VULTRA_DECLARE_SPLAT_META_BUFFER
#define VULTRA_DECLARE_SPLAT_POINT_DRAW_BUFFER
#if USE_SCENE_DEPTH
#define VULTRA_DECLARE_DEPTH_TEXTURE
#endif
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform _PushConstants
{
    uint totalPointCount;
    uint maxOutputCount;
    float frustumDilation;
    float alphaCullThreshold;
    float sizeCullingMinPixels;
    float splatScale;
    float maxAxisPixels;
} u_PC;

layout(set = 0, binding = 17, std430) buffer SortKeys
{
    uint keys[];
} s_SortKeys;

layout(set = 0, binding = 18, std430) buffer SortValues
{
    uint values[];
} s_SortValues;

layout(set = 0, binding = 20, std430) buffer VisibleCount
{
    uint count;
} s_VisibleCount;

const float kDepthRejectEpsilon = 1e-4;

uint encodeMinMaxFp32(float val)
{
    uint bits = floatBitsToUint(val);
    bits ^= uint((int(bits) >> 31) | int(0x80000000u));
    return bits;
}

void main()
{
    uint globalPointIndex = gl_GlobalInvocationID.x;
    if (globalPointIndex >= u_PC.totalPointCount)
        return;

    uint drawId = s_SplatPointDraws.drawIds[globalPointIndex];
    DrawRecord d = s_Draws.draws[drawId];
    GaussianSplatMeta splatMeta = s_SplatMeta.metas[d.primitiveIndex];
    uint localPointIndex = globalPointIndex - d.instanceIndex;
    uint sourcePointIndex = splatMeta.pointOffset + localPointIndex;

    vec3 centerObj   = s_SplatCenters.centers[sourcePointIndex].xyz;
    mat4 modelView   = u_Camera.view * d.model;
    vec4 viewCenter4 = modelView * vec4(centerObj, 1.0);
    vec3 viewCenter  = viewCenter4.xyz;
    vec4 clipCenter  = u_Camera.projection * viewCenter4;
    vec3 ndcCenter   = clipCenter.xyz / max(clipCenter.w, 1e-6);

    vec2 packedAlpha = unpackHalf2x16(s_SplatColors.colors[sourcePointIndex].y);
    float alpha = packedAlpha.y;
    float viewDepth = -viewCenter.z;

    if (alpha < u_PC.alphaCullThreshold)
        return;
    if (viewDepth <= 1e-6)
        return;
    float clip = 1.0 + u_PC.frustumDilation;
    if (abs(ndcCenter.x) > clip || abs(ndcCenter.y) > clip || ndcCenter.z < (0.0 - u_PC.frustumDilation))
        return;

    // Radius/size-based culling intentionally disabled for far-field stability.

#if USE_SCENE_DEPTH
    vec2 uv = ndcCenter.xy * 0.5 + 0.5;
    if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))))
    {
        ivec2 pixelCoord = clamp(ivec2(uv * u_Camera.resolution.xy), ivec2(0), ivec2(u_Camera.resolution.xy) - 1);
        float sceneDepth = texelFetch(u_DepthTexture, pixelCoord, 0).r;

        float frontViewZ = min(viewCenter.z, -1e-4);
        vec4 frontClip   = u_Camera.projection * vec4(viewCenter.xy, frontViewZ, 1.0);
        float frontDepth = frontClip.z / max(frontClip.w, 1e-6);

        if (sceneDepth < frontDepth - kDepthRejectEpsilon)
            return;
    }
#endif

    uint outIndex = atomicAdd(s_VisibleCount.count, 1u);
    if (outIndex >= u_PC.maxOutputCount)
        return;

    // Pair with source-over blend: back-to-front order.
    s_SortKeys.keys[outIndex] = encodeMinMaxFp32(viewCenter.z);
    s_SortValues.values[outIndex] = globalPointIndex;
}
