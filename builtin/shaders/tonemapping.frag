#version 460 core

#include "lib/color.glsl"

layout (location = 0) in vec2 v_TexCoord;

layout (set = 3, binding = 0) uniform sampler2D t_0;

layout (location = 0) out vec4 FragColor;

layout (push_constant) uniform PushConstants {
    float exposure;
    int   method; // 0: Khronos PBR Neutral, 1: ACES, 2: Reinhard (legacy)
} pc;

void main() {
    vec4 source = texture(t_0, v_TexCoord);
    source.rgb *= pc.exposure;
    vec3 color = toneMapping(source.rgb, pc.method);
    FragColor = vec4(color, 1.0);
}