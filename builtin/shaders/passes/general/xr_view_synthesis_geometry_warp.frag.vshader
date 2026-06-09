[vshader]
id       = "builtin/general/xr_view_synthesis_geometry_warp.frag"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

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
