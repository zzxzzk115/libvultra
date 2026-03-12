[vshader]
language = glsl
version = 460

[frag]
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

layout (set = 3, binding = 0) uniform sampler2D t_0;

void main() {
    const vec4 source = texture(t_0, v_TexCoord);
	FragColor = vec4(source.rgb, 1.0);
}