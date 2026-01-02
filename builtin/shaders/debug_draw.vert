#version 460 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;
layout(location = 0) out vec3 v_Color;

layout(push_constant) uniform PushConstants { mat4 u_ViewProjection; };

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    v_Color     = a_Color;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}