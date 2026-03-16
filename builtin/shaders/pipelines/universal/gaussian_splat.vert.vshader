[vshader]
language = glsl
version = 460

[vert]
#define VULTRA_DECLARE_CAMERA
#include "include/common/gpu_scene.glsl"

layout(set = 0, binding = 18, std430) readonly buffer SortedPointIds
{
    uint ids[];
} s_SortedPointIds;

struct ProjectedSplat
{
    vec4 clipNdc; // ndc.xy, clip.zw
    vec4 basis;   // basis1Px.xy, basis2Px.xy
    vec4 color;   // shaded rgb, alpha
};

layout(set = 0, binding = 19, std430) readonly buffer ProjectedSplats
{
    ProjectedSplat splats[];
} s_Projected;

layout(location = 0) out vec2 v_FragPos;
layout(location = 1) flat out uint v_SplatIndex;
layout(location = 2) out vec4 v_SplatColor;

const vec2 kCorners[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    const float kExtentStdDev       = 2.8284271247461903;
    const float kRasterSkipMinAxisPx = 0.0;
    const float kRasterSkipNdcSlack  = 1.15;

    uint sortIndex = uint(gl_InstanceIndex);
    uint projectedIndex = s_SortedPointIds.ids[sortIndex];

    vec2 corner = kCorners[gl_VertexIndex];
    ProjectedSplat projected = s_Projected.splats[projectedIndex];

    vec2 ndc0 = projected.clipNdc.xy;
    vec2 basis1Px = projected.basis.xy;
    vec2 basis2Px = projected.basis.zw;
    float clipZ = projected.clipNdc.z;
    float clipW = projected.clipNdc.w;

    float axis1 = length(basis1Px);
    float axis2 = length(basis2Px);
    float axisUpper = max(axis1, axis2);

    bool skipRaster = false;
    skipRaster = skipRaster || (projected.color.a <= 0.0);
    skipRaster = skipRaster || (clipW <= 0.0);
    skipRaster = skipRaster || (axisUpper < kRasterSkipMinAxisPx);
    skipRaster = skipRaster || (abs(ndc0.x) > kRasterSkipNdcSlack) || (abs(ndc0.y) > kRasterSkipNdcSlack);

    // Emit a clipped position to avoid raster/fragment cost for rejected splats.
    if (skipRaster)
    {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        v_FragPos = vec2(0.0);
        v_SplatIndex = projectedIndex;
        v_SplatColor = vec4(0.0);
        return;
    }

    vec2 offsetPx  = basis1Px * corner.x + basis2Px * corner.y;
    vec2 offsetNdc = offsetPx * (2.0 * u_Camera.resolution.zw);

    gl_Position = vec4((ndc0 + offsetNdc) * clipW, clipZ, clipW);

    v_FragPos = corner * kExtentStdDev;
    v_SplatIndex = projectedIndex;
    v_SplatColor = projected.color;
}
