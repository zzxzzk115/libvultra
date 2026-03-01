#ifndef VULTRA_GPU_SCENE_GLSL
#define VULTRA_GPU_SCENE_GLSL

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "bda_vertex.glsl"

// ============================================================================
// gpu_scene.glsl
//
// Minimal GPU-driven scene binding contract for built-in renderer.
//
// Goals:
//   - Multi-draw indirect + gl_DrawID indexing
//   - Vertex/index pulling via buffer device address (BDA)
//   - Material indirection via a table + byte-addressed parameter pool
//
// Notes:
//   - Descriptor set/binding indices are chosen for built-in shaders.
//     Keep these consistent with libvultra's Built-in Renderer bindings.
//   - Material parameter decoding is intentionally minimal for TestMaterialPass.
// ============================================================================

#ifndef VULTRA_SCENE_SET
#define VULTRA_SCENE_SET 0
#endif

#ifndef VULTRA_CAMERA_BINDING
#define VULTRA_CAMERA_BINDING 0
#endif

#ifndef VULTRA_DRAW_BINDING
#define VULTRA_DRAW_BINDING 1
#endif

#ifndef VULTRA_MATERIAL_TABLE_BINDING
#define VULTRA_MATERIAL_TABLE_BINDING 2
#endif

#ifndef VULTRA_MATERIAL_PARAMS_BINDING
#define VULTRA_MATERIAL_PARAMS_BINDING 3
#endif

// --------------------------------------------------------------------------
// Camera
// --------------------------------------------------------------------------

layout(set = VULTRA_SCENE_SET, binding = VULTRA_CAMERA_BINDING) uniform Camera
{
    mat4 viewProj;
} u_Camera;

// --------------------------------------------------------------------------
// Draw record
// --------------------------------------------------------------------------

// IMPORTANT: std430 alignment rules + scalar_block_layout.
// Keep host-side struct layout identical.
struct DrawRecord
{
    uint64_t vertexAddress;   // VkDeviceAddress of VertexBuffer
    uint64_t indexAddress;    // VkDeviceAddress of IndexBuffer

    uint vertexByteOffset;
    uint vertexStrideBytes;

    uint materialIndex;       // index into MaterialTable
    uint vertexCount;

    uint firstIndex;
    uint indexCount;

    uint flags;
    uint padding0;

    // Per-draw transform
    mat4 model;
};

layout(set = VULTRA_SCENE_SET, binding = VULTRA_DRAW_BINDING, std430) readonly buffer DrawBuffer
{
    DrawRecord draws[];
} s_Draws;
// --------------------------------------------------------------------------
// Material table + parameter pool
// --------------------------------------------------------------------------

// Keep enum values aligned with vultra::resource::GpuMaterialModel.
#define VULTRA_MAT_INVALID 0u
#define VULTRA_MAT_PBRMR   1u
#define VULTRA_MAT_PBRSG   2u
#define VULTRA_MAT_UNLIT   3u
#define VULTRA_MAT_PHONG   4u

struct MaterialEntry
{
    uint model;            // VULTRA_MAT_*
    uint blockOffsetBytes; // byte offset into MaterialParams
    uint tableIndex;       // optional
    uint reserved;
};

layout(set = VULTRA_SCENE_SET, binding = VULTRA_MATERIAL_TABLE_BINDING, std430) readonly buffer MaterialTable
{
    MaterialEntry materials[];
} s_Materials;

layout(set = VULTRA_SCENE_SET, binding = VULTRA_MATERIAL_PARAMS_BINDING, std430) readonly buffer MaterialParams
{
    uint words[]; // byte-addressed via 32-bit words
} s_MaterialParams;

// --------------------------------------------------------------------------
// Byte-address load helpers for parameter pool
// --------------------------------------------------------------------------

float _load_f32(uint w)
{
    return uintBitsToFloat(w);
}

uint _load_u32(uint baseByteOffset, uint byteOffset)
{
    uint addr = (baseByteOffset + byteOffset) >> 2u;
    return s_MaterialParams.words[addr];
}

float _load_f32_bytes(uint baseByteOffset, uint byteOffset)
{
    return uintBitsToFloat(_load_u32(baseByteOffset, byteOffset));
}

vec4 load_vec4_bytes(uint baseByteOffset, uint byteOffset)
{
    uint addr = (baseByteOffset + byteOffset) >> 2u;

    return vec4(
        uintBitsToFloat(s_MaterialParams.words[addr + 0u]),
        uintBitsToFloat(s_MaterialParams.words[addr + 1u]),
        uintBitsToFloat(s_MaterialParams.words[addr + 2u]),
        uintBitsToFloat(s_MaterialParams.words[addr + 3u])
    );
}

// --------------------------------------------------------------------------
// Material parameter structs
// --------------------------------------------------------------------------

struct MaterialParamsPBRMR
{
    vec4 baseColor;

    float metallicFactor;
    float roughnessFactor;

    uint baseColorTex;
    uint normalTex;
    uint mrTex;
    uint occlusionTex;
    uint emissiveTex;

    uint pad0;
    uint pad1;
};

struct MaterialParamsPBRSG
{
    vec4 diffuseColor;

    vec3 specularFactor;
    float glossinessFactor;

    uint diffuseColorTex;
    uint specularGlossinessTex;

    uint pad0;
    uint pad1;
};

struct MaterialParamsUnlit
{
    vec4 color;

    uint colorTex;

    uint pad0;
    uint pad1;
    uint pad2;
};

struct MaterialParamsPhong
{
    vec4 diffuse;

    vec4 specularShininess; // xyz = specular, w = shininess

    uint diffuseTex;

    uint pad0;
    uint pad1;
    uint pad2;
};

// --------------------------------------------------------------------------
// Material access helpers
// --------------------------------------------------------------------------

uint get_material_model(uint materialIndex)
{
    return s_Materials.materials[materialIndex].model;
}

MaterialParamsPBRMR get_pbrmr_params(uint materialIndex)
{
    MaterialEntry m = s_Materials.materials[materialIndex];

    MaterialParamsPBRMR params;

    // offset + 0 : vec4 baseColor
    params.baseColor = load_vec4_bytes(m.blockOffsetBytes, 0u);

    // offset + 16 : float metallicFactor
    params.metallicFactor = _load_f32_bytes(m.blockOffsetBytes, 16u);

    // offset + 20 : float roughnessFactor
    params.roughnessFactor = _load_f32_bytes(m.blockOffsetBytes, 20u);

    // offset + 24 : uint baseColorTex
    params.baseColorTex = _load_u32(m.blockOffsetBytes, 24u);

    // offset + 28 : uint normalTex
    params.normalTex = _load_u32(m.blockOffsetBytes, 28u);

    // offset + 32 : uint mrTex
    params.mrTex = _load_u32(m.blockOffsetBytes, 32u);

    // offset + 36 : uint occlusionTex
    params.occlusionTex = _load_u32(m.blockOffsetBytes, 36u);

    // offset + 40 : uint emissiveTex
    params.emissiveTex = _load_u32(m.blockOffsetBytes, 40u);

    return params;
}

MaterialParamsPBRSG get_pbrsg_params(uint materialIndex)
{
    MaterialEntry m = s_Materials.materials[materialIndex];

    MaterialParamsPBRSG params;

    // offset + 0 : vec4 diffuseColor
    params.diffuseColor = load_vec4_bytes(m.blockOffsetBytes, 0u);

    // offset + 16 : vec3 specularFactor
    params.specularFactor = vec3(
        _load_f32_bytes(m.blockOffsetBytes, 16u),
        _load_f32_bytes(m.blockOffsetBytes, 20u),
        _load_f32_bytes(m.blockOffsetBytes, 24u)
    );

    // offset + 28 : float glossinessFactor
    params.glossinessFactor = _load_f32_bytes(m.blockOffsetBytes, 28u);

    // offset + 32 : uint diffuseColorTex
    params.diffuseColorTex = _load_u32(m.blockOffsetBytes, 32u);

    // offset + 36 : uint specularGlossinessTex
    params.specularGlossinessTex = _load_u32(m.blockOffsetBytes, 36u);

    return params;
}

MaterialParamsUnlit get_unlit_params(uint materialIndex)
{
    MaterialEntry m = s_Materials.materials[materialIndex];

    MaterialParamsUnlit params;

    // offset + 0 : vec4 color
    params.color = load_vec4_bytes(m.blockOffsetBytes, 0u);

    // offset + 16 : uint colorTex
    params.colorTex = _load_u32(m.blockOffsetBytes, 16u);

    return params;
}

MaterialParamsPhong get_phong_params(uint materialIndex)
{
    MaterialEntry m = s_Materials.materials[materialIndex];

    MaterialParamsPhong params;

    // offset + 0 : vec4 diffuse
    params.diffuse = load_vec4_bytes(m.blockOffsetBytes, 0u);

    // offset + 16 : vec4 specularShininess
    params.specularShininess = load_vec4_bytes(m.blockOffsetBytes, 16u);

    // offset + 32 : uint diffuseTex
    params.diffuseTex = _load_u32(m.blockOffsetBytes, 32u);

    return params;
}

#ifndef VULTRA_TEXTURE_SET
#define VULTRA_TEXTURE_SET 3
#endif

#ifndef VULTRA_BINDLESS_TEXTURES_BINDING
#define VULTRA_BINDLESS_TEXTURES_BINDING 4
#endif

// Bindless texture array
layout(set = VULTRA_TEXTURE_SET, binding = VULTRA_BINDLESS_TEXTURES_BINDING) uniform sampler2D bindlessTextures[];

#define getBindlessTexture(idx) bindlessTextures[nonuniformEXT(idx)]

#endif // VULTRA_GPU_SCENE_GLSL
