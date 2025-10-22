#ifndef GBUFFER_GLSL
#define GBUFFER_GLSL

#extension GL_EXT_nonuniform_qualifier : require

#include "resources/mesh_constants.glsl"
#include "color.glsl"
#include "texture.glsl"

layout (location = 0) out vec3 g_Albedo;
layout (location = 1) out vec3 g_Normal;
layout (location = 2) out vec3 g_Emissive;
layout (location = 3) out vec3 g_MetallicRoughnessAO;
layout (location = 4) out vec3 g_TextureLodDebug;

layout (location = 0) in vec3 v_Color;
layout (location = 1) in vec2 v_TexCoord;
layout (location = 3) in mat3 v_TBN;

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
	float lod = calculateMipLevelsGL(duvdx, duvdy, textureDimensions);

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
#ifdef ENABLE_ALPHA_MASKING
	if (albedo.a < material.alphaCutoff)
	{
		discard;
	}
#endif
	g_Albedo = sRGBToLinear(albedo.rgb); // Manually convert to linear, since we load textures as UNorm

	// Normal
	vec3 normal = normalize(v_TBN[2]);
	if (material.normalIndex > 0)
	{
		vec3 normalColor = textureGrad(textures[nonuniformEXT(material.normalIndex)], v_TexCoord, duvdx, duvdy).xyz;
		vec3 tangentNormal = normalColor * 2.0 - 1.0; // Transform from [0,1] to [-1,1], tangent space
		normal = normalize(v_TBN * tangentNormal); // Transform to world space
	}
	g_Normal = normal;

	// Emissive
	vec4 emissive = vec4(0.0);
	if (material.emissiveIndex > 0)
	{
		emissive = textureGrad(textures[nonuniformEXT(material.emissiveIndex)], v_TexCoord, duvdx, duvdy);
	}
	g_Emissive = sRGBToLinear(emissive.rgb); // Manually convert to linear, since we load textures as UNorm

	// Metallic + Roughness + AO
	float metallic = 0.0; // Default metallic
	float roughness = 0.5; // Default roughness
	if (material.metallicIndex > 0)
	{
		metallic = textureGrad(textures[nonuniformEXT(material.metallicIndex)], v_TexCoord, duvdx, duvdy).r; // Assuming metallic is stored in the R channel
	}
	if (material.roughnessIndex > 0)
	{
		roughness = textureGrad(textures[nonuniformEXT(material.roughnessIndex)], v_TexCoord, duvdx, duvdy).r; // Assuming roughness is stored in the R channel
	}
	if (material.metallicRoughnessIndex > 0)
	{
		metallic = textureGrad(textures[nonuniformEXT(material.metallicRoughnessIndex)], v_TexCoord, duvdx, duvdy).b; // Metallic stored in the B channel
		roughness = textureGrad(textures[nonuniformEXT(material.metallicRoughnessIndex)], v_TexCoord, duvdx, duvdy).g; // Roughness stored in the G channel
	}

	metallic *= material.metallicFactor;
	roughness *= material.roughnessFactor;

	float ao = 1.0; // Default AO
	if (material.specularIndex > 0)
	{
		metallic = textureGrad(textures[nonuniformEXT(material.specularIndex)], v_TexCoord, duvdx, duvdy).b; // Specular stored in the B channel
		roughness = textureGrad(textures[nonuniformEXT(material.specularIndex)], v_TexCoord, duvdx, duvdy).g; // Roughness stored in the G channel
	}
	if (material.aoIndex > 0)
	{
		ao = textureGrad(textures[nonuniformEXT(material.aoIndex)], v_TexCoord, duvdx, duvdy).r; // Ambient Occlusion
	}
	g_MetallicRoughnessAO = vec3(metallic, roughness, ao);

	// float lod = textureQueryLod(t_Diffuse, v_TexCoord).x;
	g_TextureLodDebug = lodColors[int(lod)];
}

#endif