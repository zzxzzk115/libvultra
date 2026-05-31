[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE_LOD(tex, uv, layer, lod) textureLod(tex, vec3((uv), float(layer)), lod)
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE_LOD(tex, uv, layer, lod) textureLod(tex, uv, lod)
#endif

layout(location = 0) in vec2 v_SourceUv;
layout(location = 1) in float v_Valid;

layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Source;

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

void main()
{
    if (currentView() == u_PC.sourceView)
    {
        const vec2 uv = gl_FragCoord.xy / max(u_PC.resolution, vec2(1.0));
        FragColor = VULTRA_SAMPLE_LOD(u_Source, uv, sourceLayer(), 0.0);
        return;
    }

    const vec4 sampleValue = VULTRA_SAMPLE_LOD(u_Source, clamp(v_SourceUv, vec2(0.0), vec2(1.0)), sourceLayer(), 0.0);
    FragColor = vec4(sampleValue.rgb, sampleValue.a * clamp(v_Valid, 0.0, 1.0));
}
