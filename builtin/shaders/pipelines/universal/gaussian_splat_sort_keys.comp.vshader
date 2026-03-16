[vshader]
language = glsl
version = 460

[keywords]
SPLAT_SH_DEGREE_0 : bool permute
SPLAT_SH_DEGREE_1 : bool permute
SPLAT_SH_DEGREE_2 : bool permute
SPLAT_SH_DEGREE_3 : bool permute
SPLAT_OUTPUT_SRGB : bool permute

[comp]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_SPLAT_CENTER_BUFFER
#define VULTRA_DECLARE_SPLAT_COVARIANCE_BUFFER
#define VULTRA_DECLARE_SPLAT_COLOR_BUFFER
#define VULTRA_DECLARE_SPLAT_SH_BUFFER
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform _PushConstants
{
    uint  drawId;
    uint  pointCount;
    uint  valueBase;
    uint  shDegree;
} u_PC;

layout(set = 0, binding = 17, std430) buffer SortKeys
{
    uint keys[];
} s_SortKeys;

layout(set = 0, binding = 18, std430) buffer SortValues
{
    uint values[];
} s_SortValues;

struct ProjectedSplat
{
    vec4 clipNdc; // ndc.xy, clip.zw
    vec4 basis;   // basis1Px.xy, basis2Px.xy
    vec4 color;   // shaded rgb, alpha
};

layout(set = 0, binding = 19, std430) buffer ProjectedSplats
{
    ProjectedSplat splats[];
} s_Projected;

layout(set = 0, binding = 20, std430) buffer VisibleCount
{
    uint count;
} s_VisibleCount;

#if SPLAT_SH_DEGREE_0
#define SPLAT_SH_DEGREE 0
#elif SPLAT_SH_DEGREE_1
#define SPLAT_SH_DEGREE 1
#elif SPLAT_SH_DEGREE_3
#define SPLAT_SH_DEGREE 3
#else
#define SPLAT_SH_DEGREE 2
#endif

#if SPLAT_OUTPUT_SRGB
#define SPLAT_OUTPUT_SRGB_ENABLED 1
#else
#define SPLAT_OUTPUT_SRGB_ENABLED 0
#endif

const float kNdcSlack   = 1.10;
const float kNearReject = -0.02;
const float kAlphaCull  = 1.0 / 64.0;
const float kExtentStdDev = 2.8284271;
// Keep far-field/background splats: avoid culling by projected size.
const float kMinAxisPx    = 0.0;
const float kMaxAxisPx    = 512.0;
const float kMipVarPx2    = 0.10;
const float kScreenCullPadPx = 0.0;

const float SH_C1 = 0.4886025119029199;
const float SH_C2[5] = float[5](1.0925484, -1.0925484, 0.3153916, -1.0925484, 0.5462742);
const float SH_C3[7] = float[7](
    -0.5900435899266435,
    2.890611442640554,
    -0.4570457994644658,
    0.3731763325901154,
    -0.4570457994644658,
    1.445305721320277,
    -0.5900435899266435);

uint floatToOrderedUint(float f)
{
    uint x = floatBitsToUint(f);
    uint mask = ((x & 0x80000000u) != 0u) ? 0xffffffffu : 0x80000000u;
    return x ^ mask;
}

vec3 srgbToLinear(vec3 c)
{
    c = clamp(c, vec3(0.0), vec3(1.0));
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    return vec3(cutoff.x ? lo.x : hi.x, cutoff.y ? lo.y : hi.y, cutoff.z ? lo.z : hi.z);
}

vec3 shCoeff(uint pointIndex, int coeffIndex)
{
    uint idx = pointIndex * 15u + uint(coeffIndex);
    uvec2 packed = s_SplatSH.sh[idx];
    vec2 rg = unpackHalf2x16(packed.x);
    vec2 b0 = unpackHalf2x16(packed.y);
    return vec3(rg.x, rg.y, b0.x);
}

vec3 evalShRest(uint pointIndex, vec3 dir)
{
#if SPLAT_SH_DEGREE == 0
    return vec3(0.0);
#else

    float x = dir.x;
    float y = dir.y;
    float z = dir.z;

    vec3 rgb = vec3(0.0);

    rgb += SH_C1 * (-shCoeff(pointIndex, 0) * y + shCoeff(pointIndex, 1) * z - shCoeff(pointIndex, 2) * x);

#if SPLAT_SH_DEGREE < 2
    return rgb;
#endif

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float yz = y * z;
    float xz = x * z;

    rgb += (SH_C2[0] * xy) * shCoeff(pointIndex, 3) + (SH_C2[1] * yz) * shCoeff(pointIndex, 4) +
           (SH_C2[2] * (2.0 * zz - xx - yy)) * shCoeff(pointIndex, 5) + (SH_C2[3] * xz) * shCoeff(pointIndex, 6) +
           (SH_C2[4] * (xx - yy)) * shCoeff(pointIndex, 7);

#if SPLAT_SH_DEGREE < 3
    return rgb;
#endif

    rgb += SH_C3[0] * shCoeff(pointIndex, 8) * (3.0 * xx - yy) * y +
           SH_C3[1] * shCoeff(pointIndex, 9) * (x * y * z) +
           SH_C3[2] * shCoeff(pointIndex, 10) * (4.0 * zz - xx - yy) * y +
           SH_C3[3] * shCoeff(pointIndex, 11) * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) +
           SH_C3[4] * shCoeff(pointIndex, 12) * x * (4.0 * zz - xx - yy) +
           SH_C3[5] * shCoeff(pointIndex, 13) * (xx - yy) * z +
           SH_C3[6] * shCoeff(pointIndex, 14) * x * (xx - 3.0 * yy);

    return rgb;
#endif
}

void main()
{
    uint localPointIndex = gl_GlobalInvocationID.x;
    if (localPointIndex >= u_PC.pointCount)
        return;

    DrawRecord d = s_Draws.draws[u_PC.drawId];

    vec3 centerObj   = s_SplatCenters.centers[localPointIndex].xyz;
    vec3 worldCenter = (d.model * vec4(centerObj, 1.0)).xyz;
    vec3 viewCenter  = (u_Camera.view * vec4(worldCenter, 1.0)).xyz;

    uint sortKey = 0u;

    vec2 ba = unpackHalf2x16(s_SplatColors.colors[localPointIndex].y);
    float alpha = ba.y;

    float w = -viewCenter.z;
    float ndcX = 0.0;
    float ndcY = 0.0;
    if (w > 1e-6)
    {
        ndcX = (u_Camera.projection[0][0] * viewCenter.x) / w;
        ndcY = (u_Camera.projection[1][1] * viewCenter.y) / w;
    }

    bool rejected = false;
    rejected = rejected || (alpha < kAlphaCull);
    rejected = rejected || (w <= 1e-6);
    rejected = rejected || (viewCenter.z >= kNearReject);
    rejected = rejected || (ndcX < -kNdcSlack) || (ndcX > kNdcSlack) || (ndcY < -kNdcSlack) || (ndcY > kNdcSlack);

    float m11 = 0.0;
    float m12 = 0.0;
    float m13 = 0.0;
    float m22 = 0.0;
    float m23 = 0.0;
    float m33 = 0.0;

    if (!rejected)
    {
        uvec4 cv = s_SplatCovariances.covariances[localPointIndex];
        vec2 v0 = unpackHalf2x16(cv.x); // m11, m12
        vec2 v1 = unpackHalf2x16(cv.y); // m13, m22
        vec2 v2 = unpackHalf2x16(cv.z); // m23, m33

        m11 = v0.x;
        m12 = v0.y;
        m13 = v1.x;
        m22 = v1.y;
        m23 = v2.x;
        m33 = v2.y;

        float sigmaObjBound = sqrt(max(m11 + m22 + m33, 0.0));

        float invZ = 1.0 / max(-viewCenter.z, 1e-6);
        float pxPerWorldX = 0.5 * u_Camera.resolution.x * abs(u_Camera.projection[0][0]) * invZ;
        float pxPerWorldY = 0.5 * u_Camera.resolution.y * abs(u_Camera.projection[1][1]) * invZ;
        float pxPerWorld = max(pxPerWorldX, pxPerWorldY);

        float axisPxUpper = kExtentStdDev * sigmaObjBound * pxPerWorld;
        axisPxUpper = min(axisPxUpper, kMaxAxisPx);

        if (kMinAxisPx > 0.0)
            rejected = rejected || (axisPxUpper < kMinAxisPx);
    }

    if (rejected)
        return;

    sortKey = floatToOrderedUint(viewCenter.z);
    uint outIndex = atomicAdd(s_VisibleCount.count, 1u);

    ProjectedSplat outProjected;

        vec4 clip0 = u_Camera.projection * vec4(viewCenter, 1.0);
        // Force far-plane ignore: keep surviving splats inside clip z<=w to avoid fixed-function far clipping.
        clip0.z = min(clip0.z, clip0.w * 0.999999);
        vec2 ndc0  = clip0.xy / clip0.w;

        mat3 sigmaObj = mat3(m11, m12, m13,
                             m12, m22, m23,
                             m13, m23, m33);
        mat3 model3 = mat3(d.model);
        mat3 sigmaW = model3 * sigmaObj * transpose(model3);
        mat3 view3  = mat3(u_Camera.view);
        mat3 sigmaC = view3 * sigmaW * transpose(view3);

        float invZ  = 1.0 / max(-viewCenter.z, 1e-6);
        float invZ2 = invZ * invZ;
        float p00 = u_Camera.projection[0][0];
        float p11 = u_Camera.projection[1][1];

        vec3 jx = vec3(p00 * invZ, 0.0, p00 * viewCenter.x * invZ2);
        vec3 jy = vec3(0.0, p11 * invZ, p11 * viewCenter.y * invZ2);

        float sx = 0.5 * u_Camera.resolution.x;
        float sy = 0.5 * u_Camera.resolution.y;
        vec3 jxP = jx * sx;
        vec3 jyP = jy * sy;

        vec3 scJx = sigmaC * jxP;
        vec3 scJy = sigmaC * jyP;

        float a = dot(jxP, scJx);
        float b = dot(jxP, scJy);
        float c = dot(jyP, scJy);

        float det2 = max(a * c - b * b, 1e-12);
        float a2 = a + kMipVarPx2;
        float c2 = c + kMipVarPx2;
        float det2i = max(a2 * c2 - b * b, 1e-12);
        float alphaAdj = clamp(alpha * sqrt(det2 / det2i), 0.0, 1.0);

        a = max(a2, 1e-6);
        c = max(c2, 1e-6);

        float tr    = a + c;
        float det   = a * c - b * b;
        float disc  = max(0.0, 0.25 * tr * tr - det);
        float sdisc = sqrt(disc);

        float l1 = max(1e-6, 0.5 * tr + sdisc);
        float l2 = max(1e-6, 0.5 * tr - sdisc);

        vec2 e1;
        if (abs(b) > 1e-12)
            e1 = normalize(vec2(b, l1 - a));
        else
            e1 = (a >= c) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
        vec2 e2 = vec2(-e1.y, e1.x);

        float ax1 = min(kExtentStdDev * sqrt(l1), kMaxAxisPx);
        float ax2 = min(kExtentStdDev * sqrt(l2), kMaxAxisPx);
        vec2 basis1Px = e1 * ax1;
        vec2 basis2Px = e2 * ax2;

        vec3 cameraWorldPosition = u_Camera.inverseView[3].xyz;
        vec3 viewDir = normalize(worldCenter - cameraWorldPosition);

        uvec2 packedColor = s_SplatColors.colors[localPointIndex];
        vec2 rg = unpackHalf2x16(packedColor.x);
        vec2 ba2 = unpackHalf2x16(packedColor.y);
        vec3 baseColor = max(vec3(rg.x, rg.y, ba2.x), vec3(0.0));
        vec3 shaded = max(baseColor + evalShRest(localPointIndex, viewDir), vec3(0.0));
    #if SPLAT_OUTPUT_SRGB_ENABLED
        shaded = srgbToLinear(shaded);
    #endif
        float alphaOut = max(alphaAdj, alpha);

        outProjected.clipNdc = vec4(ndc0, clip0.z, clip0.w);
        outProjected.basis = vec4(basis1Px, basis2Px);
        outProjected.color = vec4(shaded, alphaOut);

    s_SortKeys.keys[outIndex]     = sortKey;
    s_SortValues.values[outIndex] = outIndex;
    s_Projected.splats[outIndex] = outProjected;
}
