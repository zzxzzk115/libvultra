#version 460 core

#include "lib/color.glsl"

layout (location = 0) in vec2 v_TexCoord;

layout (set = 3, binding = 0) uniform sampler2D t_0;

layout (location = 0) out vec4 FragColor;

void main() {
    vec4 source = texture(t_0, v_TexCoord);
#ifdef GAMMA_CORRECTION
    source = linearTosRGB(source);
#else
    source = sRGBToLinear(source);
#endif
    FragColor = vec4(source.rgb, 1.0);
}
