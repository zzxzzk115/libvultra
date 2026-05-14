[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute
USE_DIRECT_PREFIX : bool permute
USE_FOVEATED_LAYER_OUTPUT : bool permute

[comp]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_DRAW_BUFFER
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_PACKED_SOURCE_BUFFER
#if !USE_DIRECT_PREFIX
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BUFFER
#endif
#if USE_FOVEATED_LAYER_OUTPUT
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_FOVEATED_LAYER_BUFFERS
#else
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BUFFER_READWRITE
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BUFFER_READWRITE
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BUFFER_READWRITE
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BUFFER
#endif
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SH_BUFFER
#if USE_MULTIVIEW
#define VULTRA_DECLARE_STEREO_CAMERA
#endif
#include "include/common/gpu_scene.glsl"
#include "include/common/gaussian_splat_foveated.glsl"

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

const float MIN_VISIBLE_OPACITY = 0.02;
const uint SORT_ORDER_Z_DEPTH = 0u;
const uint SORT_ORDER_DISTANCE = 1u;
const uint SORT_ORDER_VIEW_DEPTH = 2u;
const uint SORT_ORDER_CONSERVATIVE_DEPTH = 3u;
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
    uint rankTotalCount;
    uint foveatedClodEnabled;
    vec4 foveatedGazeAndRings;
    vec4 foveatedLevelsAndTransition;
} u_PC;

struct EyePreprocessResult
{
    vec2 v1;
    vec2 v2;
    vec2 centerNdc;
    float eccentricityDegrees;
    float depth;
    vec4 colorOpacity;
    float sortDepth;
    bool visible;
};

float computeFoveatedEccentricityDegrees(const vec2 centerNdc, const CameraData camera)
{
    const vec2 tanHalfFov = vec2(1.0 / max(abs(camera.projection[0][0]), 1e-5),
                                 1.0 / max(abs(camera.projection[1][1]), 1e-5));
    return gaussianFoveatedEccentricityDegreesFromNdc(centerNdc, u_PC.foveatedGazeAndRings.xy, tanHalfFov);
}

float foveatedClodLevelForEccentricity(const float eccentricityDegrees)
{
    return gaussianFoveatedClodLevel(eccentricityDegrees,
                                     u_PC.foveatedGazeAndRings.zw,
                                     u_PC.foveatedLevelsAndTransition.xyz,
                                     u_PC.foveatedLevelsAndTransition.w);
}

bool passesFoveatedClodLevel(const uint rank, const float level)
{
    if (u_PC.foveatedClodEnabled == 0u)
        return true;

    const uint rankTotalCount = max(u_PC.rankTotalCount, 1u);
    const uint budget = uint(ceil(float(rankTotalCount) * level));
    return rank < budget;
}

bool passesFoveatedLayerClod(const uint layer, const uint rank)
{
    const vec3 ringLevels = clamp(u_PC.foveatedLevelsAndTransition.xyz, vec3(0.0), vec3(1.0));
    const float layerLevel = layer == 0u ? ringLevels.x : (layer == 1u ? ringLevels.y : ringLevels.z);
    return passesFoveatedClodLevel(rank, layerLevel);
}

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

float computeGeneralGaussianSplatSortDepth(const vec4 posClip,
                                           const vec4 posView,
                                           const mat3 sigmaWorld,
                                           const mat3 viewLinear,
                                           const float zFar,
                                           const uint sortOrder)
{
    if (sortOrder == SORT_ORDER_DISTANCE)
        return max(zFar - length(posView.xyz), 0.0);

    const float viewDepth = max(-posView.z, 0.0);
    if (sortOrder == SORT_ORDER_VIEW_DEPTH)
        return max(zFar - viewDepth, 0.0);

    if (sortOrder == SORT_ORDER_CONSERVATIVE_DEPTH)
    {
        const mat3 sigmaView = viewLinear * sigmaWorld * transpose(viewLinear);
        const float depthRadius = 3.0 * sqrt(max(sigmaView[2][2], 0.0));
        return max(zFar - max(viewDepth - depthRadius, 0.0), 0.0);
    }

    return max(zFar - posClip.z, 0.0);
}

EyePreprocessResult preprocessEye(const GeneralGaussianSplatPackedSource src,
                                  const GeneralGaussianSplatDrawRecord draw,
                                  const vec3 localPos,
                                  const vec3 worldPos,
                                  const mat3 modelLinear,
                                  const vec3 modelTranslation,
                                  const float lodWeight,
                                  const uint rank,
                                  const CameraData camera)
{
    EyePreprocessResult result;
    result.v1 = vec2(0.0);
    result.v2 = vec2(0.0);
    result.centerNdc = vec2(2.0);
    result.eccentricityDegrees = 180.0;
    result.depth = 1.0;
    result.colorOpacity = vec4(0.0);
    result.sortDepth = 0.0;
    result.visible = false;

    vec4 colorOpacity = decodeGeneralGaussianSplatBaseColorOpacity(src);
    colorOpacity.a *= max(draw.params0.z, 0.0);
    // Ordered CLOD encodes transition fade as an opacity multiplier. Geometry,
    // covariance and SH evaluation stay identical to the raw splat path.
    colorOpacity.a *= lodWeight;
    if (colorOpacity.a < MIN_VISIBLE_OPACITY)
        return result;

    const vec4 posView = camera.view * vec4(worldPos, 1.0);
    const vec4 posClip = camera.projection * posView;
    if (posClip.w <= 1e-5)
        return result;

    const vec3 centerNdc = posClip.xyz / posClip.w;
    const float bounds = 1.2 * posClip.w;
    if (centerNdc.z <= 0.0 || centerNdc.z >= 1.0)
        return result;
    if (posClip.x < -bounds || posClip.x > bounds || posClip.y < -bounds || posClip.y > bounds)
        return result;
#if !USE_FOVEATED_LAYER_OUTPUT
    const float eccentricityDegrees = computeFoveatedEccentricityDegrees(centerNdc.xy, camera);
    if (!passesFoveatedClodLevel(rank, foveatedClodLevelForEccentricity(eccentricityDegrees)))
        return result;
    result.eccentricityDegrees = eccentricityDegrees;
#else
    result.eccentricityDegrees = computeFoveatedEccentricityDegrees(centerNdc.xy, camera);
#endif

    const vec2 viewport = camera.resolution.xy;
    const vec2 focal =
        0.5 * vec2(abs(camera.projection[0][0]) * viewport.x, abs(camera.projection[1][1]) * viewport.y);

    const mat3 sigmaLocal = decodeGeneralGaussianSplatCovariance(src);
    const mat3 sigmaWorld = modelLinear * sigmaLocal * transpose(modelLinear);
    const mat3 viewLinear = mat3(camera.view);
    const mat3 J          = buildJacobian(posView.xyz, focal);
    const mat3 W          = transpose(mat3(camera.view[0].xyz, camera.view[1].xyz, camera.view[2].xyz));
    const mat3 T          = W * J;
    const mat3 cov        = transpose(T) * sigmaWorld * T;

    const float kernelSize = max(draw.params0.x, 1e-4);
    const float det0 = max(1e-6, cov[0][0] * cov[1][1] - cov[0][1] * cov[0][1]);
    const float det1 =
        max(1e-6, (cov[0][0] + kernelSize) * (cov[1][1] + kernelSize) - cov[0][1] * cov[0][1]);
    if (det0 <= 1e-6 || det1 <= 1e-6)
        return result;

    colorOpacity.a *= sqrt(det0 / (det1 + 1e-6) + 1e-6);
    if (colorOpacity.a < MIN_VISIBLE_OPACITY)
        return result;

    const float diagonal1 = cov[0][0] + kernelSize;
    const float offDiagonal = cov[0][1];
    const float diagonal2 = cov[1][1] + kernelSize;
    const float mid = 0.5 * (diagonal1 + diagonal2);
    const float radius = length(vec2((diagonal1 - diagonal2) * 0.5, offDiagonal));
    const float lambda1 = max(mid + radius, 1e-4);
    const float lambda2 = max(mid - radius, 0.1);

    vec2 diagonalVector = vec2(offDiagonal, lambda1 - diagonal1);
    if (length(diagonalVector) < 1e-6)
        diagonalVector = vec2(1.0, 0.0);
    else
        diagonalVector = normalize(diagonalVector);

    const float cutoffScale = max(draw.params0.y, 1e-3);
    result.v1 = sqrt(2.0 * lambda1) * diagonalVector * cutoffScale;
    result.v2 = sqrt(2.0 * lambda2) * vec2(diagonalVector.y, -diagonalVector.x) * cutoffScale;

    const vec3 cameraWorld = camera.inverseView[3].xyz;
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

    result.centerNdc = centerNdc.xy;
    result.depth = centerNdc.z;
    result.colorOpacity = colorOpacity;
    result.colorOpacity.rgb = evaluateGeneralGaussianSplatColor(dirLocal, src, min(draw.shDegree, 3u));
    result.sortDepth = computeGeneralGaussianSplatSortDepth(posClip,
                                                            posView,
                                                            sigmaWorld,
                                                            viewLinear,
                                                            camera.zFar,
                                                            uint(round(clamp(draw.params0.w, 0.0, 3.0))));
    result.visible = true;
    return result;
}

void packEyeResult(const EyePreprocessResult eye,
                   const uint sourceIndex,
                   const uint drawIndex,
                   out uvec4 packed0,
                   out uvec4 packed1)
{
    if (!eye.visible)
    {
        packed0 = uvec4(packHalf2x16(vec2(0.0)),
                        packHalf2x16(vec2(0.0)),
                        packHalf2x16(vec2(2.0)),
                        floatBitsToUint(1.0));
        packed1 = uvec4(0u, 0u, sourceIndex, drawIndex);
        return;
    }

    packed0 = uvec4(packHalf2x16(eye.v1 / u_Camera.resolution.xy),
                    packHalf2x16(eye.v2 / u_Camera.resolution.xy),
                    packHalf2x16(eye.centerNdc),
                    floatBitsToUint(eye.depth));
    packed1 = uvec4(packHalf2x16(eye.colorOpacity.rg),
                    packHalf2x16(eye.colorOpacity.ba),
                    sourceIndex,
                    drawIndex);
}

bool isInsideFoveatedLayerEccentricity(const uint layer, const float eccentricityDegrees)
{
    return gaussianFoveatedLayerContains(layer,
                                         eccentricityDegrees,
                                         u_PC.foveatedGazeAndRings.zw,
                                         u_PC.foveatedLevelsAndTransition.w);
}

float combinedFoveatedEccentricity(const EyePreprocessResult eye0, const EyePreprocessResult eye1)
{
    if (eye0.visible && eye1.visible)
        return min(eye0.eccentricityDegrees, eye1.eccentricityDegrees);
    if (eye0.visible)
        return eye0.eccentricityDegrees;
    return eye1.eccentricityDegrees;
}

void writeVisibleSplat(const EyePreprocessResult eye0,
                       const EyePreprocessResult eye1,
                       const uint sourceIndex,
                       const uint drawIndex,
                       const uint visibleIndex,
                       const float sortDepth,
                       inout GeneralGaussianSplatVisibleSplat visibleSplat,
                       inout uint sortKey,
                       inout uint sortIndex)
{
    packEyeResult(eye0,
                  sourceIndex,
                  drawIndex,
                  visibleSplat.packedEye0_0,
                  visibleSplat.packedEye0_1);
    packEyeResult(eye1,
                  sourceIndex,
                  drawIndex,
                  visibleSplat.packedEye1_0,
                  visibleSplat.packedEye1_1);
    sortKey = floatBitsToUint(sortDepth);
    sortIndex = visibleIndex;
}

#if USE_FOVEATED_LAYER_OUTPUT
void writeFoveatedLayerSplat(const uint layer,
                             const EyePreprocessResult eye0,
                             const EyePreprocessResult eye1,
                             const uint sourceIndex,
                             const uint drawIndex,
                             const uint rank,
                             const float sortDepth)
{
    if (!passesFoveatedLayerClod(layer, rank))
        return;

    uint visibleIndex = 0u;
    if (layer == 0u)
        visibleIndex = atomicAdd(s_GeneralGaussianSplatFoveatedFoveaVisibleCount.visibleCount, 1u);
    else if (layer == 1u)
        visibleIndex = atomicAdd(s_GeneralGaussianSplatFoveatedMidVisibleCount.visibleCount, 1u);
    else
        visibleIndex = atomicAdd(s_GeneralGaussianSplatFoveatedOuterVisibleCount.visibleCount, 1u);

    if (visibleIndex >= u_PC.maxVisibleSplats)
    {
        if (layer == 0u)
            atomicMin(s_GeneralGaussianSplatFoveatedFoveaVisibleCount.visibleCount, u_PC.maxVisibleSplats);
        else if (layer == 1u)
            atomicMin(s_GeneralGaussianSplatFoveatedMidVisibleCount.visibleCount, u_PC.maxVisibleSplats);
        else
            atomicMin(s_GeneralGaussianSplatFoveatedOuterVisibleCount.visibleCount, u_PC.maxVisibleSplats);
        return;
    }

    if (layer == 0u)
    {
        writeVisibleSplat(eye0,
                          eye1,
                          sourceIndex,
                          drawIndex,
                          visibleIndex,
                          sortDepth,
                          s_GeneralGaussianSplatFoveatedFoveaVisibleSplats.splats[visibleIndex],
                          s_GeneralGaussianSplatFoveatedFoveaSortKeys.keys[visibleIndex],
                          s_GeneralGaussianSplatFoveatedFoveaSortIndices.indices[visibleIndex]);
    }
    else if (layer == 1u)
    {
        writeVisibleSplat(eye0,
                          eye1,
                          sourceIndex,
                          drawIndex,
                          visibleIndex,
                          sortDepth,
                          s_GeneralGaussianSplatFoveatedMidVisibleSplats.splats[visibleIndex],
                          s_GeneralGaussianSplatFoveatedMidSortKeys.keys[visibleIndex],
                          s_GeneralGaussianSplatFoveatedMidSortIndices.indices[visibleIndex]);
    }
    else
    {
        writeVisibleSplat(eye0,
                          eye1,
                          sourceIndex,
                          drawIndex,
                          visibleIndex,
                          sortDepth,
                          s_GeneralGaussianSplatFoveatedOuterVisibleSplats.splats[visibleIndex],
                          s_GeneralGaussianSplatFoveatedOuterSortKeys.keys[visibleIndex],
                          s_GeneralGaussianSplatFoveatedOuterSortIndices.indices[visibleIndex]);
    }
}
#endif

void main()
{
    const uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_PC.pointCount)
        return;

    // Single-asset Ordered CLOD can directly consume the physically sorted
    // packed-source prefix. Baseline and multi-asset fallback use the selected
    // source table to preserve draw/source indirection.
#if USE_DIRECT_PREFIX
    const uint sourceIndex = idx;
    const uint drawIndex = 0u;
    const float lodWeight = 1.0;
#else
    const GeneralGaussianSplatSelectedSource selection = s_GeneralGaussianSplatSelectedSources.sources[idx];
    if ((selection.flags & GENERAL_GAUSSIAN_SPLAT_SELECTED_FLAG_INVALID) != 0u)
        return;
    const uint sourceIndex = selection.sourceIndex;
    const uint drawIndex = selection.drawIndex;
    // Zero defaults to full opacity so non-CLOD/baseline entries can use the
    // same packed structure without needing an extra initialization path.
    const float lodWeight = selection.packedWeight == 0u ? 1.0 : clamp(uintBitsToFloat(selection.packedWeight), 0.0, 1.0);
#endif

    {
        const GeneralGaussianSplatPackedSource src = s_GeneralGaussianSplatPackedSources.points[sourceIndex];
        const GeneralGaussianSplatDrawRecord draw  = s_GeneralGaussianSplatDraws.draws[drawIndex];
        const vec3 localPos                        = decodeGeneralGaussianSplatPosition(src);
        const mat4 model                           = draw.model;
        const mat3 modelLinear                     = mat3(model);
        const vec3 modelTranslation                = model[3].xyz;
        const vec3 worldPos                        = (model * vec4(localPos, 1.0)).xyz;
        const EyePreprocessResult eye0 =
            preprocessEye(src, draw, localPos, worldPos, modelLinear, modelTranslation, lodWeight, idx, u_Camera);
#if USE_MULTIVIEW
        const EyePreprocessResult eye1 =
            preprocessEye(src,
                          draw,
                          localPos,
                          worldPos,
                          modelLinear,
                          modelTranslation,
                          lodWeight,
                          idx,
                          u_StereoCameraBlock.cameras[1]);
#else
        const EyePreprocessResult eye1 = eye0;
#endif
        const bool visible = eye0.visible || eye1.visible;
        if (!visible)
            return;

        float sortDepth = eye0.visible ? eye0.sortDepth : 0.0;
#if USE_MULTIVIEW
        if (eye1.visible)
            sortDepth = max(sortDepth, eye1.sortDepth);
#endif

#if USE_FOVEATED_LAYER_OUTPUT
        const float eccentricityDegrees = combinedFoveatedEccentricity(eye0, eye1);
        if (isInsideFoveatedLayerEccentricity(0u, eccentricityDegrees))
            writeFoveatedLayerSplat(0u, eye0, eye1, sourceIndex, drawIndex, idx, sortDepth);
        if (isInsideFoveatedLayerEccentricity(1u, eccentricityDegrees))
            writeFoveatedLayerSplat(1u, eye0, eye1, sourceIndex, drawIndex, idx, sortDepth);
        if (isInsideFoveatedLayerEccentricity(2u, eccentricityDegrees))
            writeFoveatedLayerSplat(2u, eye0, eye1, sourceIndex, drawIndex, idx, sortDepth);
#else
        const uint visibleIndex = atomicAdd(s_GeneralGaussianSplatVisibleCount.visibleCount, 1u);
        if (visibleIndex >= u_PC.maxVisibleSplats)
        {
            atomicMin(s_GeneralGaussianSplatVisibleCount.visibleCount, u_PC.maxVisibleSplats);
            return;
        }

        writeVisibleSplat(eye0,
                          eye1,
                          sourceIndex,
                          drawIndex,
                          visibleIndex,
                          sortDepth,
                          s_GeneralGaussianSplatVisibleSplats.splats[visibleIndex],
                          s_GeneralGaussianSplatSortKeys.keys[visibleIndex],
                          s_GeneralGaussianSplatSortIndices.indices[visibleIndex]);
#endif

    }
}
