#version 460 core

#include "resources/mesh_constants.glsl"

layout (location = 0) out vec3 g_Albedo;
layout (location = 1) out vec3 g_Normal;
layout (location = 2) out vec3 g_Emissive;
layout (location = 3) out vec3 g_MetallicRoughnessAO;

layout (location = 0) in vec3 v_Color;
layout (location = 1) in vec2 v_TexCoord;
layout (location = 3) in mat3 v_TBN;

// Diffuse
layout (set = 3, binding = 0) uniform sampler2D t_Diffuse;

// Metallic
layout (set = 3, binding = 1) uniform sampler2D t_Metallic;

// Roughness
layout (set = 3, binding = 2) uniform sampler2D t_Roughness;

// Normal map
layout (set = 3, binding = 3) uniform sampler2D t_Normal;

// Ambient Occlusion
layout (set = 3, binding = 4) uniform sampler2D t_AO;

// Emissive
layout (set = 3, binding = 5) uniform sampler2D t_Emissive;

// MetallicRoughness
layout (set = 3, binding = 6) uniform sampler2D t_MetallicRoughness;

// Specular (PBR)
// R: AO
// G: Roughness
// B: Metallic
layout (set = 3, binding = 7) uniform sampler2D t_Specular;

void main() {
	// Albedo
	vec4 albedo = vec4(0.0);
	if (c_Mesh.useAlbedoTexture)
	{
		albedo = texture(t_Diffuse, v_TexCoord);
	}
	else
	{
		albedo = vec4(c_Mesh.baseColor.rgb * v_Color, c_Mesh.baseColor.a);
	}
	if (albedo.a < 0.5)
	{
		discard;
	}
	g_Albedo = albedo.rgb;

	// Normal
	vec3 normal = normalize(v_TBN[2]);
	if (c_Mesh.useNormalTexture)
	{
		vec3 normalColor = texture(t_Normal, v_TexCoord).xyz;
		vec3 tangentNormal = normalColor * 2.0 - 1.0;
		normal = normalize(tangentNormal * transpose(v_TBN));
	}
	g_Normal = normal;

	// Emissive
	vec4 emissive = vec4(0.0);
	if (c_Mesh.useEmissiveTexture)
	{
		emissive = texture(t_Emissive, v_TexCoord);
	}
	g_Emissive = emissive.rgb;

	// Metallic + Roughness + AO
	float metallic = c_Mesh.metallicFactor; // Default metallic
	float roughness = c_Mesh.roughnessFactor; // Default roughness
	if (c_Mesh.useMetallicTexture)
	{
		metallic = texture(t_Metallic, v_TexCoord).r; // Assuming metallic is stored in the R channel
	}
	if (c_Mesh.useRoughnessTexture)
	{
		roughness = texture(t_Roughness, v_TexCoord).r; // Assuming roughness is stored in the R channel
	}
	if (c_Mesh.useMetallicRoughnessTexture)
	{
		metallic = texture(t_MetallicRoughness, v_TexCoord).b; // Metallic stored in the B channel
		roughness = texture(t_MetallicRoughness, v_TexCoord).g; // Roughness stored in the G channel
	}
	float ao = 1.0; // Default AO
	if (c_Mesh.useSpecularTexture)
	{
		metallic = texture(t_Specular, v_TexCoord).b; // Specular stored in the B channel
		roughness = texture(t_Specular, v_TexCoord).g; // Roughness stored in the G channel
		ao = texture(t_Specular, v_TexCoord).r; // AO stored in the R channel
	}
	if (c_Mesh.useAOTexture)
	{
		ao = texture(t_AO, v_TexCoord).r; // Ambient Occlusion
	}
	g_MetallicRoughnessAO = vec3(metallic, roughness, ao);
}