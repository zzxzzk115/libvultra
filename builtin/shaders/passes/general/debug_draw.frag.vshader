[vshader]
id       = "builtin/general/debug_draw.frag"
language = glsl
version = 460

[frag]
layout(location = 0) in vec3 v_Color;
layout(location = 0) out vec4 FragColor;

void main() { FragColor = vec4(v_Color, 1.0); }
