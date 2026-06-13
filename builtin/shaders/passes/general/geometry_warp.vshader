[vshader]
id       = "builtin/general/geometry_warp"
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
    // Single-target (mono / explicit-per-eye) render: there is no gl_ViewIndex, so select
    // the target eye's view-projection explicitly from the requested targetView. This lets
    // one mono pass synthesize the right eye (targetViewProj[1]) from a left-eye source.
    return (u_PC.targetView == XR_VIEW_RIGHT) ? 1u : 0u;
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

[geom]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#endif

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec2 v_SourceUv[];

layout(location = 0) out vec2     g_SourceUv;
layout(location = 1) flat out int g_Valid;

layout(push_constant) uniform XrGeometryWarpPushConstants
{
    vec2  resolution;
    uint  sourceView;
    uint  targetView;
    uint  gridSize;
    float sideLenThreshold;
    uint  useDepthAware;
} u_PC;

void main()
{
    // A grid cell that straddles a depth discontinuity stretches after warping;
    // classify such primitives as holes by their longest warped edge.
    const float e0 = length(gl_in[0].gl_Position.xy - gl_in[1].gl_Position.xy);
    const float e1 = length(gl_in[0].gl_Position.xy - gl_in[2].gl_Position.xy);
    const float e2 = length(gl_in[1].gl_Position.xy - gl_in[2].gl_Position.xy);
    const float maxSideLen = max(e0, max(e1, e2));
    const bool  valid      = maxSideLen <= u_PC.sideLenThreshold;

    const float maxDepth = max(gl_in[0].gl_Position.z, max(gl_in[1].gl_Position.z, gl_in[2].gl_Position.z));

    for (int i = 0; i < 3; ++i)
    {
        g_SourceUv  = v_SourceUv[i];
        g_Valid     = valid ? 1 : 0;
        gl_Position = gl_in[i].gl_Position;

        if (u_PC.useDepthAware != 0u)
        {
            // Remap depth so the fragment alpha can encode validity + depth:
            // valid -> [0, 0.5], stretched/hole -> (0.5, 1].
            gl_Position.z = valid ? (gl_Position.z * 0.5) : (maxDepth * 0.5 + 0.5);
        }
        else
        {
            // Binary validity only: push holes to the far plane.
            gl_Position.z = valid ? gl_Position.z : 1.0;
        }

        EmitVertex();
    }
    EndPrimitive();
}

[frag]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE(tex, uv, layer) textureLod(tex, vec3((uv), float(layer)), 0.0)
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE(tex, uv, layer) textureLod(tex, uv, 0.0)
#endif

layout(location = 0) in vec2     g_SourceUv;
layout(location = 1) flat in int g_Valid;

layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Source;

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

uint sourceLayer()
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    if (u_PC.sourceView == XR_VIEW_RIGHT)
        return 1u;
#endif
    return 0u;
}

// Alpha-validity convention shared with the pull-push inpaint stage:
//   [0, 0.5]   valid    (depth = a * 2)
//   (0.5, 1)   invalid  (depth = (a - 0.5) * 2)
//   1.0        hole     (uncovered: comes from the attachment clear)
const float kAlphaClassEps = 1.0 / 255.0;

void main()
{
    // Sample the source at the interpolated source UV for full-resolution color
    // (the grid mesh only drives the warp, not the color resolution).
    vec4 color = VULTRA_SAMPLE(u_Source, clamp(g_SourceUv, vec2(0.0), vec2(1.0)), sourceLayer());

    if (u_PC.useDepthAware != 0u)
    {
        if (g_Valid != 0)
            color.a = clamp(gl_FragCoord.z, 0.0, 0.5 - kAlphaClassEps);
        else
            color.a = clamp(gl_FragCoord.z, 0.5 + kAlphaClassEps, 1.0 - 1e-6);
    }
    else
    {
        color.a = (g_Valid != 0) ? 0.0 : 1.0;
    }

    FragColor = color;
}
