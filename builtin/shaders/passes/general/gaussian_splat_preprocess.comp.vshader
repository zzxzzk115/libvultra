[vshader]
language = glsl
version = 460

[comp]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_DRAW_BUFFER
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_PACKED_SOURCE_BUFFER
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BUFFER_READWRITE
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BUFFER_READWRITE
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BUFFER_READWRITE
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BUFFER
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_DISPATCH_ARGS_BUFFER
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SH_BUFFER
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

const float MIN_VISIBLE_OPACITY = 0.02;
const float SH_C1 = 0.4886025119029199;
const float SH_C2[5] = float[5](1.0925484305920792,
                                -1.0925484305920792,
                                0.31539156525252005,
                                -1.0925484305920792,
                                0.5462742152960396);
const float SH_C3[7] = float[7](-0.5900435899266435,
                                2.890611442640554,
                                -0.4570457994644658,
                                0.3731763325901154,
                                -0.4570457994644658,
                                1.445305721320277,
                                -0.5900435899266435);

layout(push_constant) uniform GeneralGaussianSplatPreprocessPushConstants
{
    uint pointCount;
    uint maxVisibleSplats;
    uint padding0;
    uint padding1;
} u_PC;

mat3 buildJacobian(const vec3 camspace, const vec2 focal)
{
    float z = camspace.z;
    if (abs(z) < 1e-4)
        z = z < 0.0 ? -1e-4 : 1e-4;
    return mat3(vec3(focal.x / z, 0.0, -(focal.x * camspace.x) / (z * z)),
                vec3(0.0, -focal.y / z, (focal.y * camspace.y) / (z * z)),
                vec3(0.0));
}

vec3 evaluateGeneralGaussianSplatColor(const vec3 dir, const GeneralGaussianSplatPackedSource src, const uint shDegree)
{
    vec3 result = decodeGeneralGaussianSplatBaseColorOpacity(src).rgb;
    if (shDegree == 0u)
        return max(result, vec3(0.0));

    const float x = dir.x;
    const float y = dir.y;
    const float z = dir.z;
    const uint shOffset = src.aux0.z;

    result += -SH_C1 * y * decodeGeneralGaussianSplatShCoeff(shOffset + 0u) +
              SH_C1 * z * decodeGeneralGaussianSplatShCoeff(shOffset + 1u) -
              SH_C1 * x * decodeGeneralGaussianSplatShCoeff(shOffset + 2u);

    if (shDegree > 1u)
    {
        const float xx = x * x;
        const float yy = y * y;
        const float zz = z * z;
        const float xy = x * y;
        const float yz = y * z;
        const float xz = x * z;

        result += SH_C2[0] * xy * decodeGeneralGaussianSplatShCoeff(shOffset + 3u) +
                  SH_C2[1] * yz * decodeGeneralGaussianSplatShCoeff(shOffset + 4u) +
                  SH_C2[2] * (2.0 * zz - xx - yy) * decodeGeneralGaussianSplatShCoeff(shOffset + 5u) +
                  SH_C2[3] * xz * decodeGeneralGaussianSplatShCoeff(shOffset + 6u) +
                  SH_C2[4] * (xx - yy) * decodeGeneralGaussianSplatShCoeff(shOffset + 7u);
    }

    if (shDegree > 2u)
    {
        const float xx = x * x;
        const float yy = y * y;
        const float zz = z * z;
        const float xy = x * y;
        const float yz = y * z;
        const float xz = x * z;

        result += SH_C3[0] * y * (3.0 * xx - yy) * decodeGeneralGaussianSplatShCoeff(shOffset + 8u) +
                  SH_C3[1] * xy * z * decodeGeneralGaussianSplatShCoeff(shOffset + 9u) +
                  SH_C3[2] * y * (4.0 * zz - xx - yy) * decodeGeneralGaussianSplatShCoeff(shOffset + 10u) +
                  SH_C3[3] * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * decodeGeneralGaussianSplatShCoeff(shOffset + 11u) +
                  SH_C3[4] * x * (4.0 * zz - xx - yy) * decodeGeneralGaussianSplatShCoeff(shOffset + 12u) +
                  SH_C3[5] * z * (xx - yy) * decodeGeneralGaussianSplatShCoeff(shOffset + 13u) +
                  SH_C3[6] * x * (xx - 3.0 * yy) * decodeGeneralGaussianSplatShCoeff(shOffset + 14u);
    }

    return max(result, vec3(0.0));
}

void main()
{
    const uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_PC.pointCount)
        return;

    const GeneralGaussianSplatPackedSource src = s_GeneralGaussianSplatPackedSources.points[idx];
    const GeneralGaussianSplatDrawRecord draw  = s_GeneralGaussianSplatDraws.draws[src.aux0.y];
    const vec3 localPos                        = decodeGeneralGaussianSplatPosition(src);
    const mat4 model                           = draw.model;
    const mat3 modelLinear                     = mat3(model);
    const vec3 modelTranslation                = model[3].xyz;
    const vec3 worldPos                        = (model * vec4(localPos, 1.0)).xyz;
    vec4 colorOpacity                          = decodeGeneralGaussianSplatBaseColorOpacity(src);
    colorOpacity.a *= max(draw.params0.z, 0.0);
    if (colorOpacity.a < MIN_VISIBLE_OPACITY)
        return;

    const vec4 posView = u_Camera.view * vec4(worldPos, 1.0);
    const vec4 posClip = u_Camera.projection * posView;
    if (posClip.w <= 1e-5)
        return;

    const vec3 centerNdc = posClip.xyz / posClip.w;
    const float bounds = 1.2 * posClip.w;
    if (centerNdc.z <= 0.0 || centerNdc.z >= 1.0)
        return;
    if (posClip.x < -bounds || posClip.x > bounds || posClip.y < -bounds || posClip.y > bounds)
        return;

    const vec2 viewport = u_Camera.resolution.xy;
    const vec2 focal =
        0.5 * vec2(abs(u_Camera.projection[0][0]) * viewport.x, abs(u_Camera.projection[1][1]) * viewport.y);

    const mat3 sigmaLocal = decodeGeneralGaussianSplatCovariance(src);
    const mat3 sigmaWorld = modelLinear * sigmaLocal * transpose(modelLinear);
    const mat3 J          = buildJacobian(posView.xyz, focal);
    const mat3 W          = transpose(mat3(u_Camera.view[0].xyz, u_Camera.view[1].xyz, u_Camera.view[2].xyz));
    const mat3 T          = W * J;
    const mat3 cov        = transpose(T) * sigmaWorld * T;

    const float kernelSize = max(draw.params0.x, 1e-4);
    const float det0 = max(1e-6, cov[0][0] * cov[1][1] - cov[0][1] * cov[0][1]);
    const float det1 =
        max(1e-6, (cov[0][0] + kernelSize) * (cov[1][1] + kernelSize) - cov[0][1] * cov[0][1]);
    if (det0 <= 1e-6 || det1 <= 1e-6)
        return;

    colorOpacity.a *= sqrt(det0 / (det1 + 1e-6) + 1e-6);
    if (colorOpacity.a < MIN_VISIBLE_OPACITY)
        return;

    const float diagonal1  = cov[0][0] + kernelSize;
    const float offDiagonal = cov[0][1];
    const float diagonal2  = cov[1][1] + kernelSize;
    const float mid        = 0.5 * (diagonal1 + diagonal2);
    const float radius     = length(vec2((diagonal1 - diagonal2) * 0.5, offDiagonal));
    const float lambda1    = max(mid + radius, 1e-4);
    const float lambda2    = max(mid - radius, 0.1);

    vec2 diagonalVector = vec2(offDiagonal, lambda1 - diagonal1);
    if (length(diagonalVector) < 1e-6)
        diagonalVector = vec2(1.0, 0.0);
    else
        diagonalVector = normalize(diagonalVector);

    const float cutoffScale = max(draw.params0.y, 1e-3);
    const vec2 v1 = sqrt(2.0 * lambda1) * diagonalVector * cutoffScale;
    const vec2 v2 = sqrt(2.0 * lambda2) * vec2(diagonalVector.y, -diagonalVector.x) * cutoffScale;

    const vec3 cameraWorld = u_Camera.inverseView[3].xyz;
    vec3 dirLocal;
    const float detLinear = determinant(modelLinear);
    if (abs(detLinear) < 1e-8)
    {
        dirLocal = worldPos - cameraWorld;
    }
    else
    {
        const vec3 cameraLocal = inverse(modelLinear) * (cameraWorld - modelTranslation);
        dirLocal = localPos - cameraLocal;
    }
    if (dot(dirLocal, dirLocal) < 1e-10)
        dirLocal = vec3(0.0, 0.0, 1.0);
    else
        dirLocal = normalize(dirLocal);
    colorOpacity.rgb = evaluateGeneralGaussianSplatColor(dirLocal, src, min(draw.shDegree, 3u));

    const uint visibleIndex = atomicAdd(s_GeneralGaussianSplatVisibleCount.visibleCount, 1u);
    if (visibleIndex >= u_PC.maxVisibleSplats)
    {
        atomicMin(s_GeneralGaussianSplatVisibleCount.visibleCount, u_PC.maxVisibleSplats);
        return;
    }

    s_GeneralGaussianSplatVisibleSplats.splats[visibleIndex].packed0 = uvec4(packHalf2x16(v1 / viewport),
                                                                              packHalf2x16(v2 / viewport),
                                                                              packHalf2x16(centerNdc.xy),
                                                                              floatBitsToUint(centerNdc.z));
    s_GeneralGaussianSplatVisibleSplats.splats[visibleIndex].packed1 =
        uvec4(packHalf2x16(colorOpacity.rg), packHalf2x16(colorOpacity.ba), idx, src.aux0.y);

    const float sortDepth = max(u_Camera.zFar - posClip.z, 0.0);
    s_GeneralGaussianSplatSortKeys.keys[visibleIndex]    = floatBitsToUint(sortDepth);
    s_GeneralGaussianSplatSortIndices.indices[visibleIndex] = visibleIndex;

    const uint keysPerGroup = 256u * 15u;
    if ((visibleIndex % keysPerGroup) == 0u)
        atomicAdd(s_GeneralGaussianSplatDispatchArgs.dispatchArgs.dispatchX, 1u);
}
