[vshader]
language = glsl
version = 460

[keywords]
USE_SORTED_IDS : bool permute

[vert]
#include "include/common/gaussian_splat.glsl"

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

layout(push_constant) uniform GaussianSplatPushConstants
{
    float frustumDilation;
    float alphaCullThreshold;
    float sizeCullingMinPixels;
    float splatScale;
    float maxAxisPixels;
    float depthIsoThreshold;
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

layout(set = 0, binding = 14, std430) readonly buffer SplatCovarianceBuffer
{
    uvec4 covariances[];
} s_SplatCovariances;

layout(set = 0, binding = 15, std430) readonly buffer SplatColorBuffer
{
    uvec2 colors[];
} s_SplatColors;

layout(set = 0, binding = 16, std430) readonly buffer SplatSHBuffer
{
    uvec2 sh[];
} s_SplatSH;

#if USE_SORTED_IDS
layout(set = 0, binding = 18, std430) readonly buffer SortedPointIds
{
    uint ids[];
} s_SortedPointIds;
#endif

layout(set = 0, binding = 19, std430) readonly buffer SplatMetaBuffer
{
    GaussianSplatMeta metas[];
} s_SplatMeta;

layout(set = 0, binding = 21, std430) readonly buffer SplatPointDrawBuffer
{
    uint drawIds[];
} s_SplatPointDraws;

layout(location = 0) out vec2 v_FragPos;
layout(location = 1) flat out uint v_SplatIndex;
layout(location = 2) out vec4 v_SplatColor;

const vec2 kCorners[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2(-1.0,  1.0),
    vec2( 1.0,  1.0)
);

const float SH_C1 = 0.4886025119029199;
const float SH_C2[5] = float[5](1.0925484, -1.0925484, 0.3153916, -1.0925484, 0.5462742);
const float SH_C3[7] = float[7](
    -0.5900435899266435,
    2.890611442640554,
    -0.4570457994644658,
    0.3731763325901154,
    -0.4570457994644658,
    1.445305721320277,
    -0.5900435899266435
);

vec3 shCoeff(uint pointIndex, int coeffIndex)
{
    uint idx = pointIndex * 15u + uint(coeffIndex);
    uvec2 packed = s_SplatSH.sh[idx];
    vec2 rg = unpackHalf2x16(packed.x);
    vec2 b0 = unpackHalf2x16(packed.y);
    return vec3(rg.x, rg.y, b0.x);
}

vec3 evalShRest(uint pointIndex, vec3 dir, uint shDegree)
{
    if (shDegree == 0u)
        return vec3(0.0);

    float x = dir.x;
    float y = dir.y;
    float z = dir.z;

    vec3 rgb = vec3(0.0);
    rgb += SH_C1 * (-shCoeff(pointIndex, 0) * y + shCoeff(pointIndex, 1) * z - shCoeff(pointIndex, 2) * x);

    if (shDegree < 2u)
        return rgb;

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float yz = y * z;
    float xz = x * z;

    rgb += (SH_C2[0] * xy) * shCoeff(pointIndex, 3) + (SH_C2[1] * yz) * shCoeff(pointIndex, 4) +
           (SH_C2[2] * (2.0 * zz - xx - yy)) * shCoeff(pointIndex, 5) + (SH_C2[3] * xz) * shCoeff(pointIndex, 6) +
           (SH_C2[4] * (xx - yy)) * shCoeff(pointIndex, 7);

    if (shDegree < 3u)
        return rgb;

    rgb += SH_C3[0] * shCoeff(pointIndex, 8) * (3.0 * xx - yy) * y +
           SH_C3[1] * shCoeff(pointIndex, 9) * (x * y * z) +
           SH_C3[2] * shCoeff(pointIndex, 10) * (4.0 * zz - xx - yy) * y +
           SH_C3[3] * shCoeff(pointIndex, 11) * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) +
           SH_C3[4] * shCoeff(pointIndex, 12) * x * (4.0 * zz - xx - yy) +
           SH_C3[5] * shCoeff(pointIndex, 13) * (xx - yy) * z +
           SH_C3[6] * shCoeff(pointIndex, 14) * x * (xx - 3.0 * yy);

    return rgb;
}

void main()
{
    uint globalPointIndex = uint(gl_InstanceIndex);
#if USE_SORTED_IDS
    globalPointIndex = s_SortedPointIds.ids[globalPointIndex];
#endif
    uint drawId = s_SplatPointDraws.drawIds[globalPointIndex];
    DrawRecord d = s_Draws.draws[drawId];
    GaussianSplatMeta splatMeta = s_SplatMeta.metas[d.primitiveIndex];
    uint localPointIndex = globalPointIndex - d.instanceIndex;
    uint sourcePointIndex = splatMeta.pointOffset + localPointIndex;

    vec3 centerObj = s_SplatCenters.centers[sourcePointIndex].xyz;
    vec3 worldCenter = (d.model * vec4(centerObj, 1.0)).xyz;
    mat4 modelView = u_Camera.camera.view * d.model;
    vec4 viewCenter4 = modelView * vec4(centerObj, 1.0);
    vec4 clipCenter = u_Camera.camera.projection * viewCenter4;

    uvec2 packedColor = s_SplatColors.colors[sourcePointIndex];
    vec2 rg = unpackHalf2x16(packedColor.x);
    vec2 ba = unpackHalf2x16(packedColor.y);
    vec4 splatColor = vec4(max(vec3(rg.x, rg.y, ba.x), vec3(0.0)), ba.y);

    if (splatColor.a < u_PC.alphaCullThreshold || clipCenter.w <= 1e-6)
    {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        v_FragPos = vec2(0.0);
        v_SplatIndex = globalPointIndex;
        v_SplatColor = vec4(0.0);
        return;
    }

    uvec4 cv = s_SplatCovariances.covariances[sourcePointIndex];
    vec2 v0 = unpackHalf2x16(cv.x);
    vec2 v1 = unpackHalf2x16(cv.y);
    vec2 v2 = unpackHalf2x16(cv.z);

    mat3 sigmaObj = mat3(
        v0.x, v0.y, v1.x,
        v0.y, v1.y, v2.x,
        v1.x, v2.x, v2.y
    );
    vec2 focal = vec2(0.5 * u_Camera.camera.resolution.x * u_Camera.camera.projection[0][0],
                      0.5 * u_Camera.camera.resolution.y * u_Camera.camera.projection[1][1]);
    vec3 cov2Dv = gaussianSplatCovarianceProjection(sigmaObj, viewCenter4, focal, modelView);

    float alphaAdj = splatColor.a;
    vec2 basis1Px;
    vec2 basis2Px;
    if (!gaussianSplatProjectedExtentBasis(
            cov2Dv, kGaussianSplatExtentStdDev, u_PC.splatScale, u_PC.maxAxisPixels, alphaAdj, basis1Px, basis2Px))
    {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        v_FragPos = vec2(0.0);
        v_SplatIndex = globalPointIndex;
        v_SplatColor = vec4(0.0);
        return;
    }

    vec3 cameraWorldPosition = u_Camera.camera.inverseView[3].xyz;
    vec3 viewDir = normalize(worldCenter - cameraWorldPosition);
    vec3 shaded = max(splatColor.rgb + evalShRest(sourcePointIndex, viewDir, min(splatMeta.shDegree, 3u)), vec3(0.0));

    vec2 ndc0 = clipCenter.xy / clipCenter.w;
    vec2 corner = kCorners[gl_VertexIndex];
    vec2 offsetPx = basis1Px * corner.x + basis2Px * corner.y;
    vec2 offsetNdc = offsetPx * (2.0 * u_Camera.camera.resolution.zw);

    float clipZ = min(clipCenter.z, clipCenter.w * 0.999999);
    gl_Position = vec4((ndc0 + offsetNdc) * clipCenter.w, clipZ, clipCenter.w);
    v_FragPos = corner * kGaussianSplatExtentStdDev;
    v_SplatIndex = globalPointIndex;
    v_SplatColor = vec4(shaded, alphaAdj);
}
