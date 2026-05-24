[vshader]
language = glsl
version = 460

[frag]
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform sampler2D u_Source;
layout(set = 3, binding = 1) uniform sampler2D u_Reflection;

void main()
{
    vec4 source = texture(u_Source, v_TexCoord);
    vec4 reflection = texture(u_Reflection, v_TexCoord);
    FragColor = vec4(source.rgb + reflection.rgb, source.a);
}
