[vshader]
id       = "builtin/general/motion_vector.frag"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_DEPTH_TEXTURE sampler2DArray
#define VULTRA_FETCH(tex, pixel, lod) texelFetch(tex, ivec3((pixel), int(gl_ViewIndex)), lod)
#define VULTRA_VIEW_INDEX int(gl_ViewIndex)
#else
#define VULTRA_DEPTH_TEXTURE sampler2D
#define VULTRA_FETCH(tex, pixel, lod) texelFetch(tex, pixel, lod)
#define VULTRA_VIEW_INDEX 0
#endif

layout(location = 0) out vec2 FragMotion;

layout(set = 3, binding = 0) uniform VULTRA_DEPTH_TEXTURE u_Depth;

// Matches MotionVectorBlock in motion_vector_pass.cpp (std140 UBO).
layout(set = 1, binding = 0) uniform MotionVectorBlock
{
    mat4 clipToPreviousClip[2];
    vec4 params; // xy = resolution, z = view 0 reset, w = view 1 reset
} u_MV;

float safe_rcp_w(float w)
{
    float s = w < 0.0 ? -1.0 : 1.0;
    return 1.0 / (abs(w) > 1e-6 ? w : s * 1e-6);
}

void main()
{
    int   viewIndex = VULTRA_VIEW_INDEX;
    float reset     = viewIndex == 0 ? u_MV.params.z : u_MV.params.w;
    if (reset != 0.0)
    {
        FragMotion = vec2(0.0);
        return;
    }

    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = VULTRA_FETCH(u_Depth, pixel, 0).r;
    if (depth >= 1.0)
    {
        FragMotion = vec2(0.0);
        return;
    }

    vec2 resolution = u_MV.params.xy;
    vec2 currentUv = gl_FragCoord.xy / resolution;
    vec4 currentClip = vec4(currentUv * 2.0 - 1.0, depth, 1.0);
    vec4 previousClip = u_MV.clipToPreviousClip[viewIndex] * currentClip;
    vec2 previousNdc = previousClip.xy * safe_rcp_w(previousClip.w);
    vec2 previousUv = previousNdc * 0.5 + 0.5;

    FragMotion = (currentUv - previousUv) * resolution;
}
