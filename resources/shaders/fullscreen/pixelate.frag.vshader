[vshader]
id       = "project/fullscreen/pixelate.frag"
language = glsl
version = 460

[properties]
pixelSize : float = 8.0 range(1.0, 64.0)
gridOffset : float = 0.5 range(0.0, 1.0)
mode : enum(Pixelate=0,Posterize=1,DebugUV=2) = Pixelate
preserveAlpha : bool = true

[frag]
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

layout (set = 3, binding = 0) uniform sampler2D t_0;

layout(push_constant) uniform PushConstants
{
    float pixelSize;
    float gridOffset;
    int mode;
    int preserveAlpha;
};

void main() {
    vec2 sourceSize = vec2(textureSize(t_0, 0));
    vec2 pixel = floor(v_TexCoord * sourceSize / max(pixelSize, 1.0)) * max(pixelSize, 1.0) + vec2(gridOffset * max(pixelSize, 1.0));
    vec2 uv = clamp(pixel / sourceSize, vec2(0.0), vec2(1.0));
    vec4 color = texture(t_0, uv);
    if (mode == 1)
        color.rgb = floor(color.rgb * 6.0) / 6.0;
    else if (mode == 2)
        color.rgb = vec3(v_TexCoord, 0.0);
    FragColor = vec4(color.rgb, preserveAlpha != 0 ? color.a : 1.0);
}
