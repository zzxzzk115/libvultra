[vshader]
id       = "builtin/general/gaussian_blur.frag"
language = glsl
version = 460

[properties]
scale : float = 1.0 range(0.0, 8.0)
horizontal : bool = true

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

layout(push_constant) uniform PushConstants
{
    float scale;     // widens the kernel footprint
    int   horizontal; // 1 = horizontal pass, 0 = vertical pass
};

// 5-tap Gaussian using linear sampling (3 texture fetches per side share two texels).
// https://rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
const float kOffsets[3] = float[](0.0, 1.3846153846, 3.2307692308);
const float kWeights[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);

void main()
{
    const vec2 texel = (1.0 / vec2(textureSize(u_Source, 0).xy)) * scale;
    const vec2 dir   = horizontal != 0 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);

    vec3 result = VULTRA_SAMPLE(u_Source, v_TexCoord).rgb * kWeights[0];
    for (int i = 1; i < 3; ++i)
    {
        const vec2 offset = dir * kOffsets[i];
        result += VULTRA_SAMPLE(u_Source, v_TexCoord + offset).rgb * kWeights[i];
        result += VULTRA_SAMPLE(u_Source, v_TexCoord - offset).rgb * kWeights[i];
    }
    FragColor = vec4(result, 1.0);
}
