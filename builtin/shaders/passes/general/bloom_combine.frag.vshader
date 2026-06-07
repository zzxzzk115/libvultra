[vshader]
id       = "builtin/general/bloom_combine.frag"
language = glsl
version = 460

[properties]
intensity : float = 0.6 range(0.0, 5.0)

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

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Source; // scene HDR color
layout(set = 3, binding = 1) uniform VULTRA_SOURCE_TEXTURE u_Bloom;  // blurred bright extraction

layout(push_constant) uniform PushConstants
{
    float intensity;
};

void main()
{
    const vec4 scene = VULTRA_SAMPLE(u_Source, v_TexCoord);
    const vec3 bloom = VULTRA_SAMPLE(u_Bloom, v_TexCoord).rgb;
    FragColor = vec4(scene.rgb + bloom * intensity, scene.a);
}
