[vshader]
id       = "builtin/general/bloom_prefilter.frag"
language = glsl
version = 460

[properties]
threshold : float = 1.0 range(0.0, 16.0)
knee : float = 0.5 range(0.0, 1.0)

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
    float threshold; // luminance below which nothing blooms
    float knee;      // soft-knee width (0 = hard cutoff)
};

// Soft-knee brightness threshold (Karis/Unreal style): pixels above `threshold`
// pass through, with a smooth ramp of width `threshold * knee` to avoid flicker.
void main()
{
    const vec3  color      = max(VULTRA_SAMPLE(u_Source, v_TexCoord).rgb, vec3(0.0));
    const float brightness = max(color.r, max(color.g, color.b));

    const float kneeWidth = threshold * knee + 1e-5;
    float       soft      = clamp(brightness - threshold + kneeWidth, 0.0, 2.0 * kneeWidth);
    soft                  = soft * soft / (4.0 * kneeWidth + 1e-5);
    const float contribution = max(soft, brightness - threshold) / max(brightness, 1e-5);

    FragColor = vec4(color * contribution, 1.0);
}
