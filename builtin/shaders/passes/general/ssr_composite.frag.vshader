[vshader]
id       = "builtin/general/ssr_composite.frag"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE(tex, uv) texture(tex, vec3((uv), float(gl_ViewIndex)))
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE(tex, uv) texture(tex, uv)
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Source;
layout(set = 3, binding = 1) uniform VULTRA_SOURCE_TEXTURE u_Reflection;

void main()
{
    vec4 source = VULTRA_SAMPLE(u_Source, v_TexCoord);
    vec4 reflection = VULTRA_SAMPLE(u_Reflection, v_TexCoord);
    FragColor = vec4(source.rgb + reflection.rgb, source.a);
}
