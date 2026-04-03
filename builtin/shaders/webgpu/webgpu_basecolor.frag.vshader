[vshader]
language = glsl
version = 460

[frag]
layout(location = 0) in vec2 v_TexCoord0;
layout(location = 0) out vec4 FragColor;
layout(set = 3, binding = 4) uniform sampler2D u_BaseColorTexture;

void main()
{
    FragColor = texture(u_BaseColorTexture, v_TexCoord0);
}
