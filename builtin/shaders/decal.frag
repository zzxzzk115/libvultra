#version 460 core

#include "resources/mesh_constants.glsl"
#include "resources/camera_block.glsl"

layout (location = 0) out vec4 FragColor;

layout (location = 0) in vec3 v_Color;
layout (location = 1) in vec2 v_TexCoord;

// BaseColor
layout (set = 3, binding = 0) uniform sampler2D t_BaseColor;

void main() {
	// Albedo
	vec4 albedo = vec4(0.0);
	if (c_Mesh.useAlbedoTexture)
	{
		vec3 baseColor = texture(t_BaseColor, v_TexCoord).rgb;
		albedo = vec4(baseColor * v_Color, c_Mesh.opacity);
	}
	else
	{
		albedo = vec4(c_Mesh.baseColor.rgb * v_Color, c_Mesh.opacity);
	}

	FragColor = albedo;
}