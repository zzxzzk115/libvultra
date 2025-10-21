#ifndef MESHLET_GLSL
#define MESHLET_GLSL

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "lib/color.glsl"
#include "lib/texture.glsl"

layout(location = 0) in flat uint meshletID;
layout(location = 1) in flat uint materialIndex;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 texCoord;
layout(location = 4) in mat3 tbn;

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
layout(std430, set = 2, binding = 0) readonly buffer Materials {
    GPUMaterial materials[];
};

layout(set = 2, binding = 1) uniform sampler2D textures[];

layout(push_constant) uniform GlobalMeshDataPushConstants {
	uint64_t vertexBufferAddress;
    uint64_t meshletBufferAddress;
    uint64_t meshletVertexBufferAddress;
    uint64_t meshletTriangleBufferAddress;

    uint meshletCount;
	uint enableNormalMapping;
	uint padding0;
	uint padding1;

	mat4 modelMatrix;
} g_Mesh;

layout (location = 0) out vec3 g_Albedo;
layout (location = 1) out vec3 g_Normal;
layout (location = 2) out vec3 g_Emissive;
layout (location = 3) out vec3 g_MetallicRoughnessAO;
layout (location = 4) out vec3 g_TextureLodDebug;
layout (location = 5) out vec4 g_MeshletDebug;

#ifndef MESHLET_DEBUG_MODE
#define MESHLET_DEBUG_MODE 0
#endif

vec3 hashColor(uint id)
{
    uint n = id * 1664525u + 1013904223u;
    return vec3((n & 0xFFu), (n >> 8) & 0xFFu, (n >> 16) & 0xFFu) / 255.0;
}

void main()
{
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

#if MESHLET_DEBUG_MODE == 0
    g_MeshletDebug = vec4(hashColor(meshletID), 1.0);
#elif MESHLET_DEBUG_MODE == 1
    g_MeshletDebug = vec4(hashColor(materialIndex), 1.0);
#endif
}

#endif // MESHLET_GLSL