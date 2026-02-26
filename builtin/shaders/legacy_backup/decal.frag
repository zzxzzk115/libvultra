#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

#include "resources/mesh_constants.glsl"
#include "resources/camera_block.glsl"

layout (location = 0) out vec4 FragColor;

layout (location = 0) in vec3 v_Color;
layout (location = 1) in vec2 v_TexCoord;

#define GPU_MATERIAL_SET 3
#define GPU_MATERIAL_BINDING 0
#include "resources/gpu_material.glsl"

layout(set = 3, binding = 1) uniform sampler2D textures[];

void main() {
	GPUMaterial material = materials[nonuniformEXT(getMaterialIndex())];

	// Manually calculate LOD for better consistency
	vec2 duvdx = dFdx(v_TexCoord);
	vec2 duvdy = dFdy(v_TexCoord);
	vec2 textureDimensions = vec2(textureSize(textures[nonuniformEXT(material.albedoIndex)], 0));

	// Albedo
	vec4 albedo = vec4(0.0);
	if (material.albedoIndex > 0)
	{
		vec4 baseColor = textureGrad(textures[nonuniformEXT(material.albedoIndex)], v_TexCoord, duvdx, duvdy);
		albedo = vec4(baseColor.rgb * v_Color, baseColor.a * material.opacity);
	}
	else
	{
		albedo = vec4(material.baseColor.rgb * v_Color, material.baseColor.a);
	}

	FragColor = albedo;
}