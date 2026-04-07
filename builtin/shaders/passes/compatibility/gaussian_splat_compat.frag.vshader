[vshader]
language = glsl
version = 460

[frag]
layout(location = 0) in vec2 v_FragPos;
layout(location = 1) flat in uint v_SplatIndex;
layout(location = 2) in vec4 v_SplatColor;
layout(location = 0) out vec4 FragColor;

vec3 hashColor(uint id)
{
    uint n = id * 1664525u + 1013904223u;
    return vec3((n & 0xFFu), (n >> 8) & 0xFFu, (n >> 16) & 0xFFu) / 255.0;
}

void main()
{
    const float kOpacityDiscardThreshold = 1.0 / 255.0;

    float A = dot(v_FragPos, v_FragPos);
    float alpha = exp(-0.5 * A) * v_SplatColor.a;
    vec3 color = (v_SplatColor.a > 0.0) ? v_SplatColor.rgb : hashColor(v_SplatIndex);
    if (A > 8.0 || alpha < kOpacityDiscardThreshold)
    {
        FragColor = vec4(0.0);
        return;
    }

    FragColor = vec4(color * alpha, alpha);
}
