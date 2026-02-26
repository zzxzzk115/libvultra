#version 460 core

#include "resources/camera_block.glsl"
#include "lib/space.glsl"

layout(location = 0) out vec3 v_EyeDirection;

void main() {
    vec2 texCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(texCoord * 2.0 - 1.0, 1.0, 1.0);

	const vec3 P = clipToView(gl_Position, u_Camera.inversedProjection).xyz;
	v_EyeDirection = viewToWorld(vec4(P, 0.0), u_Camera.inversedView);	
	gl_Position = gl_Position.xyww;
}