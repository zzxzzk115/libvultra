[vshader]
language = glsl
version = 460

[frag]
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

// @param_enum method Khronos PBR Neutral=0, ACES=1, Reinhard=2
layout (set = 3, binding = 0) uniform sampler2D t_0;

layout (push_constant) uniform TonemappingPushConstants
{
    float exposure;
    int method;
} u_PC;

vec3 toneMappingKhronosPbrNeutral(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return color;

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

vec3 toneMappingACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 toneMappingReinhard(vec3 color)
{
    return color / (color + vec3(1.0));
}

vec3 toneMapping(vec3 color, int method)
{
    if (method == 0)
        return toneMappingKhronosPbrNeutral(color);
    if (method == 1)
        return toneMappingACES(color);
    if (method == 2)
        return toneMappingReinhard(color);
    return toneMappingKhronosPbrNeutral(color);
}

void main() {
    vec4 source = texture(t_0, v_TexCoord);
    source.rgb *= max(u_PC.exposure, 0.0);
    vec3 color = toneMapping(max(source.rgb, vec3(0.0)), u_PC.method);
    FragColor = vec4(color, 1.0);
}
