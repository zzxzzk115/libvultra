[vshader]
language = glsl
version = 460

[comp]
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct CameraData
{
    mat4 projection;
    mat4 inverseProjection;
    mat4 view;
    mat4 inverseView;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 resolution;
    float zNear;
    float zFar;
    float fovY;
    float _padding;
    vec4 frustumPlanes[6];
};

struct DrawRecord
{
    uint primitiveIndex;
    uint materialIndex;
    uint vertexStrideBytes;
    uint flags;
    uvec2 vertexAddress;
    uint instanceIndex;
    uint padding0;
    mat4 model;
};

struct GaussianSplatMeta
{
    uint pointOffset;
    uint pointCount;
    uint shDegree;
    uint shRestCoeffCount;
};

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

layout(set = 0, binding = 0, std140) uniform CameraBlock
{
    CameraData camera;
} u_Camera;

layout(set = 0, binding = 1, std430) readonly buffer DrawBuffer
{
    DrawRecord draws[];
} s_Draws;

layout(set = 0, binding = 13, std430) readonly buffer SplatCenterBuffer
{
    vec4 centers[];
} s_SplatCenters;

layout(set = 0, binding = 15, std430) readonly buffer SplatColorBuffer
{
    uvec2 colors[];
} s_SplatColors;

layout(set = 0, binding = 17, std430) buffer SortKeys
{
    uint keys[];
} s_SortKeys;

layout(set = 0, binding = 18, std430) buffer SortValues
{
    uint values[];
} s_SortValues;

layout(set = 0, binding = 19, std430) readonly buffer SplatMetaBuffer
{
    GaussianSplatMeta metas[];
} s_SplatMeta;

layout(set = 0, binding = 20, std430) buffer VisibleCount
{
    uint count;
} s_VisibleCount;

layout(set = 0, binding = 21, std430) readonly buffer SplatPointDrawBuffer
{
    uint drawIds[];
} s_SplatPointDraws;

uint encodeMinMaxFp32(float val)
{
    uint bits = floatBitsToUint(val);
    if ((bits & 0x80000000u) != 0u)
        return ~bits;
    return bits | 0x80000000u;
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
    mat4 modelView   = u_Camera.camera.view * d.model;
    vec4 viewCenter4 = modelView * vec4(centerObj, 1.0);
    vec3 viewCenter  = viewCenter4.xyz;
    vec4 clipCenter  = u_Camera.camera.projection * viewCenter4;

    if (clipCenter.w <= 1e-6)
        return;

    uint outIndex = atomicAdd(s_VisibleCount.count, 1u);
    if (outIndex >= u_PC.maxOutputCount)
        return;

    s_SortKeys.keys[outIndex] = encodeMinMaxFp32(viewCenter.z);
    s_SortValues.values[outIndex] = globalPointIndex;
}
