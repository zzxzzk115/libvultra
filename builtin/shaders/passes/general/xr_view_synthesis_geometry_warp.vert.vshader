[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[vert]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_DEPTH_TEXTURE sampler2DArray
#define VULTRA_DEPTH_FETCH(tex, pixel, layer) texelFetch(tex, ivec3((pixel), int(layer)), 0).r
#else
#define VULTRA_DEPTH_TEXTURE sampler2D
#define VULTRA_DEPTH_FETCH(tex, pixel, layer) texelFetch(tex, pixel, 0).r
#endif

layout(set = 3, binding = 1) uniform VULTRA_DEPTH_TEXTURE u_Depth;

layout(location = 0) out vec2 v_SourceUv;
layout(location = 1) out float v_Valid;

layout(push_constant) uniform XrGeometryWarpPushConstants
{
    vec2 resolution;
    uint sourceView;
    uint targetView;
    uint gridSize;
    float warpStrength;
} u_PC;

const uint XR_VIEW_PRIMARY = 0u;
const uint XR_VIEW_LEFT = 1u;
const uint XR_VIEW_RIGHT = 2u;
const uint XR_VIEW_STEREO = 3u;

const vec2 kTriangleCorners[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
);

float eyeOffset(uint viewKind)
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    if (viewKind == XR_VIEW_STEREO)
        return gl_ViewIndex == 0u ? -1.0 : 1.0;
#endif
    if (viewKind == XR_VIEW_LEFT)
        return -1.0;
    if (viewKind == XR_VIEW_RIGHT)
        return 1.0;
    return 0.0;
}

uint currentView()
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    return gl_ViewIndex == 0u ? XR_VIEW_LEFT : XR_VIEW_RIGHT;
#else
    return XR_VIEW_PRIMARY;
#endif
}

uint sourceLayer()
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    if (u_PC.sourceView == XR_VIEW_RIGHT)
        return 1u;
#endif
    return 0u;
}

uint activeTargetView()
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    if (u_PC.targetView == XR_VIEW_STEREO)
        return currentView();
    if (currentView() == u_PC.sourceView)
        return currentView();
#endif
    return u_PC.targetView;
}

void main()
{
    const ivec2 imageSize = max(ivec2(u_PC.resolution), ivec2(1));
    const uint gridSize = max(u_PC.gridSize, 1u);
    const uint cellsX = max((uint(imageSize.x) + gridSize - 1u) / gridSize, 1u);
    const uint cellIndex = uint(gl_VertexIndex) / 6u;
    const uint cornerIndex = uint(gl_VertexIndex) - cellIndex * 6u;
    const uvec2 cell = uvec2(cellIndex % cellsX, cellIndex / cellsX);
    const vec2 corner = kTriangleCorners[cornerIndex];
    const vec2 pixel = min((vec2(cell) + corner) * float(gridSize), vec2(imageSize - 1));
    const ivec2 texel = ivec2(pixel);
    const float depth = clamp(VULTRA_DEPTH_FETCH(u_Depth, texel, sourceLayer()), 0.0, 1.0);

    const vec2 uv = (pixel + vec2(0.5)) / vec2(imageSize);
    vec2 ndc = uv * 2.0 - 1.0;

    const uint targetView = activeTargetView();
    const float srcEye = eyeOffset(u_PC.sourceView);
    const float dstEye = eyeOffset(targetView);
    const float disparity = (1.0 - depth) * u_PC.warpStrength;
    ndc.x += (srcEye - dstEye) * disparity * 2.0;

    v_SourceUv = uv;
    v_Valid = 1.0;
    gl_Position = vec4(ndc, depth, 1.0);
}
