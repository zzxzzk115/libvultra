[vshader]
language = glsl
version = 460

[frag]
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

layout (set = 3, binding = 0) uniform sampler2D t_0;

void main() {
    const float pixelSize = 8.0;
    vec2 sourceSize = vec2(textureSize(t_0, 0));
    vec2 pixel = floor(v_TexCoord * sourceSize / pixelSize) * pixelSize + vec2(0.5 * pixelSize);
    vec2 uv = clamp(pixel / sourceSize, vec2(0.0), vec2(1.0));
    FragColor = texture(t_0, uv);
}
