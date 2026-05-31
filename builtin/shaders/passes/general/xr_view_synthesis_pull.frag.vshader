[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE_LOD(tex, uv, lod) textureLod(tex, vec3((uv), float(gl_ViewIndex)), lod)
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE_LOD(tex, uv, lod) textureLod(tex, uv, lod)
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_LinearPyramid;
layout(set = 3, binding = 1) uniform VULTRA_SOURCE_TEXTURE u_PointPyramid;

layout(push_constant) uniform XrPullPushConstants
{
    int lod;
} u_PC;

void main()
{
    const vec4 current = VULTRA_SAMPLE_LOD(u_PointPyramid, v_TexCoord, float(u_PC.lod));
    if (current.a > 0.5)
    {
        FragColor = current;
        return;
    }

    const vec4 coarse = VULTRA_SAMPLE_LOD(u_LinearPyramid, v_TexCoord, float(u_PC.lod + 1));
    if (coarse.a > 0.0)
    {
        FragColor = coarse;
        return;
    }

    FragColor = vec4(current.rgb, 0.0);
}
