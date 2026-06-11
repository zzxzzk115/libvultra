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
#else
#define VULTRA_DEPTH_TEXTURE sampler2D
#define VULTRA_FETCH(tex, pixel, lod) texelFetch(tex, pixel, lod)
#endif

layout(location = 0) out vec2 FragMotion;

layout(set = 3, binding = 0) uniform VULTRA_DEPTH_TEXTURE u_Depth;

layout(push_constant) uniform PushConstants
{
    mat4 clipToPreviousClip;
    vec2 resolution;
    uint reset;
    uint padding0;
} u_Push;

float safe_rcp_w(float w)
{
    float s = w < 0.0 ? -1.0 : 1.0;
    return 1.0 / (abs(w) > 1e-6 ? w : s * 1e-6);
}

void main()
{
    if (u_Push.reset != 0u)
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

    vec2 currentUv = gl_FragCoord.xy / u_Push.resolution;
    vec4 currentClip = vec4(currentUv * 2.0 - 1.0, depth, 1.0);
    vec4 previousClip = u_Push.clipToPreviousClip * currentClip;
    vec2 previousNdc = previousClip.xy * safe_rcp_w(previousClip.w);
    vec2 previousUv = previousNdc * 0.5 + 0.5;

    FragMotion = (currentUv - previousUv) * u_Push.resolution;
}
