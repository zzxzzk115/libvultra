#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

#include "resources/mesh_constants.glsl"
#include "resources/camera_block.glsl"

layout (location = 0) out vec4 FragColor;

layout (location = 0) in vec3 v_Color;
layout (location = 1) in vec2 v_TexCoord;

struct GPUMaterial {
    // --- texture indices ---
    uint albedoIndex;
    uint alphaMaskIndex;
    uint metallicIndex;
    uint roughnessIndex;

    uint specularIndex;
    uint normalIndex;
    uint aoIndex;
    uint emissiveIndex;

    uint metallicRoughnessIndex;
    uint paddingUI0; // ensure 16-byte alignment
    uint paddingUI1; // ensure 16-byte alignment
    uint paddingUI2; // ensure 16-byte alignment

    // --- color vectors ---
    vec4 baseColor;
    vec4 emissiveColorIntensity;
    vec4 ambientColor;

    // --- scalar material params ---
    float opacity;
    float metallicFactor;
    float roughnessFactor;
    float ior;

    float alphaCutoff;
    float paddingF0; // ensure 16-byte alignment
    float paddingF1; // ensure 16-byte alignment
    float paddingF2; // ensure 16-byte alignment

    // --- int params ---
    int alphaMode; // 0: OPAQUE, 1: MASK, 2: BLEND
    int doubleSided;
    int paddingI0; // ensure 16-byte alignment
    int paddingI1; // ensure 16-byte alignment
};
layout(std430, set = 3, binding = 0) readonly buffer Materials {
    GPUMaterial materials[];
};

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