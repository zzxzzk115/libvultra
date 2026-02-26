#version 460 core

layout (location = 0) in vec2 v_TexCoord;

layout (set = 3, binding = 0) uniform sampler2D t_0;
layout (set = 3, binding = 1) uniform sampler2D t_1;

layout (location = 0) out vec4 FragColor;

void main() {
    const vec4 source = texture(t_0, v_TexCoord);
    const vec4 target = texture(t_1, v_TexCoord);
    FragColor = vec4(mix(source.rgb, target.rgb, 0.5), 1.0);
}