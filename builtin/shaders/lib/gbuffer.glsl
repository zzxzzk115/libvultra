#ifndef GBUFFER_GLSL
#define GBUFFER_GLSL

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
	// Manually calculate LOD for better consistency
	vec2 duvdx = dFdx(v_TexCoord);
	vec2 duvdy = dFdy(v_TexCoord);
	vec2 textureDimensions = vec2(textureSize(t_Diffuse, 0));
	float lod = calculateMipLevelsGL(duvdx, duvdy, textureDimensions);

	// Albedo
	vec4 albedo = vec4(0.0);
	if (c_Mesh.useAlbedoTexture)
	{
		vec4 baseColor = textureGrad(t_Diffuse, v_TexCoord, duvdx, duvdy);
		albedo = vec4(baseColor.rgb * v_Color, baseColor.a * c_Mesh.opacity);
	}
	else
	{
		albedo = vec4(c_Mesh.baseColor.rgb * v_Color, c_Mesh.baseColor.a);
	}
#ifdef ENABLE_ALPHA_MASKING
	if (albedo.a < 0.5)
	{
		discard;
	}
#endif
	g_Albedo = sRGBToLinear(albedo.rgb); // Manually convert to linear, since we load textures as UNorm

	// Normal
	vec3 normal = normalize(v_TBN[2]);
	if (c_Mesh.useNormalTexture)
	{
		vec3 normalColor = textureGrad(t_Normal, v_TexCoord, duvdx, duvdy).xyz;
		vec3 tangentNormal = normalColor * 2.0 - 1.0; // Transform from [0,1] to [-1,1], tangent space
		normal = normalize(v_TBN * tangentNormal); // Transform to world space
	}
	g_Normal = normal;

	// Emissive
	vec4 emissive = vec4(0.0);
	if (c_Mesh.useEmissiveTexture)
	{
		emissive = textureGrad(t_Emissive, v_TexCoord, duvdx, duvdy);
	}
	g_Emissive = sRGBToLinear(emissive.rgb); // Manually convert to linear, since we load textures as UNorm

	// Metallic + Roughness + AO
	float metallic = c_Mesh.metallicFactor; // Default metallic
	float roughness = c_Mesh.roughnessFactor; // Default roughness
	if (c_Mesh.useMetallicTexture)
	{
		metallic = textureGrad(t_Metallic, v_TexCoord, duvdx, duvdy).r; // Assuming metallic is stored in the R channel
	}
	if (c_Mesh.useRoughnessTexture)
	{
		roughness = textureGrad(t_Roughness, v_TexCoord, duvdx, duvdy).r; // Assuming roughness is stored in the R channel
	}
	if (c_Mesh.useMetallicRoughnessTexture)
	{
		metallic = textureGrad(t_MetallicRoughness, v_TexCoord, duvdx, duvdy).b; // Metallic stored in the B channel
		roughness = textureGrad(t_MetallicRoughness, v_TexCoord, duvdx, duvdy).g; // Roughness stored in the G channel
	}
	float ao = 1.0; // Default AO
	if (c_Mesh.useSpecularTexture)
	{
		metallic = textureGrad(t_Specular, v_TexCoord, duvdx, duvdy).b; // Specular stored in the B channel
		roughness = textureGrad(t_Specular, v_TexCoord, duvdx, duvdy).g; // Roughness stored in the G channel
		ao = textureGrad(t_Specular, v_TexCoord, duvdx, duvdy).r; // AO stored in the R channel
	}
	if (c_Mesh.useAOTexture)
	{
		ao = textureGrad(t_AO, v_TexCoord, duvdx, duvdy).r; // Ambient Occlusion
	}
	g_MetallicRoughnessAO = vec3(metallic, roughness, ao);

	// float lod = textureQueryLod(t_Diffuse, v_TexCoord).x;
	g_TextureLodDebug = lodColors[int(lod)];
}

#endif