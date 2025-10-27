#ifndef MESHLET_GLSL
#define MESHLET_GLSL

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "lib/color.glsl"
#include "lib/texture.glsl"

#ifdef ENABLE_EARLY_Z
layout (early_fragment_tests) in;
#endif

layout(location = 0) in flat uint meshletID;
layout(location = 1) in flat uint materialIndex;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 texCoord;
layout(location = 4) in vec3 normal;
layout(location = 5) in vec4 tangent;

#include "resources/gpu_material.glsl"

layout(set = 2, binding = 1) uniform sampler2D textures[];

layout(push_constant) uniform GlobalMeshDataPushConstants {
	uint64_t vertexBufferAddress;
    uint64_t meshletBufferAddress;
    uint64_t meshletVertexBufferAddress;
    uint64_t meshletTriangleBufferAddress;

    uint meshletCount;
	uint enableNormalMapping;
	uint debugMode;
	uint padding1;

	mat4 modelMatrix;
} g_Mesh;

layout (location = 0) out vec3 g_Albedo;
layout (location = 1) out vec3 g_Normal;
layout (location = 2) out vec3 g_Emissive;
layout (location = 3) out vec3 g_MetallicRoughnessAO;
layout (location = 4) out vec3 g_TextureLodDebug;
layout (location = 5) out vec4 g_MeshletDebug;

vec3 hashColor(uint id)
{
    uint n = id * 1664525u + 1013904223u;
    return vec3((n & 0xFFu), (n >> 8) & 0xFFu, (n >> 16) & 0xFFu) / 255.0;
}

void main()
{
    // Construct TBN matrix
    mat3 normalMatrix = transpose(inverse(mat3(g_Mesh.modelMatrix)));
    vec3 T = normalize(normalMatrix * tangent.xyz);
    vec3 N = normalize(normalMatrix * normal);
    T = normalize(T - dot(T, N) * N); // Gram-Schmidt orthogonalize
    vec3 B = cross(N, T) * tangent.w;
    mat3 tbn = mat3(T, B, N);

	GPUMaterial mat = materials[nonuniformEXT(materialIndex)];

	// Manually calculate LOD for better consistency
	vec2 duvdx = dFdx(texCoord);
	vec2 duvdy = dFdy(texCoord);
	vec2 textureDimensions = vec2(textureSize(textures[nonuniformEXT(mat.albedoIndex)], 0));
	float lod = calculateMipLevelsGL(duvdx, duvdy, textureDimensions);

	vec4 albedoWithAlpha = textureGrad(textures[nonuniformEXT(mat.albedoIndex)], texCoord, duvdx, duvdy);
    if (albedoWithAlpha.a < mat.alphaCutoff)
    {
        discard;
    }

	vec3 albedo = mat.albedoIndex > 0 ? sRGBToLinear(textureGrad(textures[nonuniformEXT(mat.albedoIndex)], texCoord, duvdx, duvdy).rgb) : sRGBToLinear(mat.baseColor.rgb);
    vec3 emissive = mat.emissiveIndex > 0 ? sRGBToLinear(textureGrad(textures[nonuniformEXT(mat.emissiveIndex)], texCoord, duvdx, duvdy).rgb) : mat.emissiveColorIntensity.rgb * mat.emissiveColorIntensity.a;
    vec3 normal = textureGrad(textures[nonuniformEXT(mat.normalIndex)], texCoord, duvdx, duvdy).rgb;
    normal = normalize(normal * 2.0 - 1.0); // normal map
    normal = mat.normalIndex > 0 && g_Mesh.enableNormalMapping == 1 ? normalize(tbn * normal) : tbn[2];
    float ao = textureGrad(textures[nonuniformEXT(mat.aoIndex)], texCoord, duvdx, duvdy).r;
    float metallic = textureGrad(textures[nonuniformEXT(mat.metallicIndex)], texCoord, duvdx, duvdy).r;
    float roughness = textureGrad(textures[nonuniformEXT(mat.roughnessIndex)], texCoord, duvdx, duvdy).r;
    if (mat.metallicRoughnessIndex > 0) {
        vec4 mrSample = textureGrad(textures[nonuniformEXT(mat.metallicRoughnessIndex)], texCoord, duvdx, duvdy);
        metallic = mrSample.b;
        roughness = mrSample.g;
    }

    metallic *= mat.metallicFactor;
    roughness *= mat.roughnessFactor;

	g_Albedo = albedo;
	g_Normal = normal;
	g_Emissive = emissive;
	g_MetallicRoughnessAO = vec3(metallic, roughness, ao);

	// float lod = textureQueryLod(t_Diffuse, v_TexCoord).x;
	g_TextureLodDebug = lodColors[int(lod)];

    if (g_Mesh.debugMode == 0)
    {
        g_MeshletDebug = vec4(hashColor(meshletID), 1.0);
    }
    else if (g_Mesh.debugMode == 1)
    {
        g_MeshletDebug = vec4(hashColor(materialIndex), 1.0);
    }
}

#endif // MESHLET_GLSL