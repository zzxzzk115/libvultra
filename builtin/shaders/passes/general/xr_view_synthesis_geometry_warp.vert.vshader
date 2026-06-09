[vshader]
id       = "builtin/general/xr_view_synthesis_geometry_warp.vert"
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

// Generic source->target reprojection, done as unproject-then-project so the
// intermediate perspective divide is exact (a single combined matrix would only
// approximate it). For stereo these come from the per-eye cameras; a temporal
// backend supplies prev-frame unproject + current-frame project (or vice versa).
layout(set = 1, binding = 0) uniform XrWarpBlock
{
    mat4 sourceInvViewProj;   // source NDC+depth -> world (homogeneous)
    mat4 targetViewProj[2];   // world -> target layer NDC
} u_Warp;

layout(location = 0) out vec2 v_SourceUv;

layout(push_constant) uniform XrGeometryWarpPushConstants
{
    vec2  resolution;
    uint  sourceView;
    uint  targetView;
    uint  gridSize;
    float sideLenThreshold;
    uint  useDepthAware;
} u_PC;

const uint XR_VIEW_RIGHT = 2u;

const vec2 kTriangleCorners[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
);

uint currentLayer()
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    return uint(gl_ViewIndex);
#else
    return 0u;
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

void main()
{
    const ivec2 imageSize = max(ivec2(u_PC.resolution), ivec2(1));
    const uint  gridSize  = max(u_PC.gridSize, 1u);
    const uint  cellsX    = max((uint(imageSize.x) + gridSize - 1u) / gridSize, 1u);
    const uint  cellIndex = uint(gl_VertexIndex) / 6u;
    const uint  cornerIdx = uint(gl_VertexIndex) - cellIndex * 6u;
    const uvec2 cell      = uvec2(cellIndex % cellsX, cellIndex / cellsX);
    const vec2  corner    = kTriangleCorners[cornerIdx];
    const vec2  pixel     = min((vec2(cell) + corner) * float(gridSize), vec2(imageSize - 1));
    const ivec2 texel     = ivec2(pixel);

    const float depth = clamp(VULTRA_DEPTH_FETCH(u_Depth, texel, sourceLayer()), 0.0, 1.0);

    const vec2 uv = (pixel + vec2(0.5)) / vec2(imageSize);
    v_SourceUv    = uv;

    // Unproject the source NDC sample to world, then project into the current
    // target layer. When the target layer is the source eye these matrices are
    // mutually inverse, so the source view renders as a straight pass-through.
    const vec2 ndc    = uv * 2.0 - 1.0;
    vec4       worldH = u_Warp.sourceInvViewProj * vec4(ndc, depth, 1.0);
    const vec3 world  = worldH.xyz / worldH.w;
    vec4       clip   = u_Warp.targetViewProj[currentLayer()] * vec4(world, 1.0);
    if (abs(clip.w) > 1e-6)
        clip /= clip.w;

    gl_Position = clip;
}
