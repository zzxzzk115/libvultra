[vshader]
language = glsl
version = 460

[vert]
#include "include/common/cpu_scene.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 3) in vec2 a_TexCoord0;

layout(location = 0) out vec2 v_TexCoord0;

void main()
{
    vec4 worldPos = u_Draw.model * vec4(a_Position, 1.0);
    gl_Position = u_CameraBlock.data.viewProjection * worldPos;
    v_TexCoord0 = a_TexCoord0;
}

