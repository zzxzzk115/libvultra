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
//   - Resource declarations are opt-in via VULTRA_DECLARE_* macros so shaders
//     only declare bindings they actually use.
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

#ifndef VULTRA_INSTANCE_BINDING
#define VULTRA_INSTANCE_BINDING 2
#endif

#ifndef VULTRA_MESH_TABLE_BINDING
#define VULTRA_MESH_TABLE_BINDING 3
#endif

#ifndef VULTRA_MESHLET_BINDING
#define VULTRA_MESHLET_BINDING 4
#endif

#ifndef VULTRA_MODEL_BINDING
#define VULTRA_MODEL_BINDING 5
#endif

#ifndef VULTRA_VISIBLE_MESHLET_BINDING
#define VULTRA_VISIBLE_MESHLET_BINDING 6
#endif

#ifndef VULTRA_VISIBLE_COUNT_BINDING
#define VULTRA_VISIBLE_COUNT_BINDING 7
#endif

#ifndef VULTRA_MATERIAL_TABLE_BINDING
#define VULTRA_MATERIAL_TABLE_BINDING 8
#endif

#ifndef VULTRA_MATERIAL_PARAMS_BINDING
#define VULTRA_MATERIAL_PARAMS_BINDING 9
#endif

#ifndef VULTRA_MESHLET_VERTEX_BINDING
#define VULTRA_MESHLET_VERTEX_BINDING 10
#endif

#ifndef VULTRA_MESHLET_TRIANGLE_BINDING
#define VULTRA_MESHLET_TRIANGLE_BINDING 11
#endif

#ifndef VULTRA_INDIRECT_BINDING
#define VULTRA_INDIRECT_BINDING 12
#endif

#ifndef VULTRA_TEXTURE_SET
#define VULTRA_TEXTURE_SET 3
#endif

#ifndef VULTRA_BINDLESS_TEXTURES_BINDING
#define VULTRA_BINDLESS_TEXTURES_BINDING 4
#endif

struct CameraData
{
    mat4 projection;
    mat4 inverseProjection;
    mat4 view;
    mat4 inverseView;
    mat4 viewProjection;
    mat4 inverseViewProjection;

    vec4 resolution;      // xy = size, zw = 1/size

    float zNear;
    float zFar;
    float fovY;
    float _padding;
    vec4 frustumPlanes[6];
};

// --------------------------------------------------------------------------
// Draw record
// --------------------------------------------------------------------------

// IMPORTANT: std430 alignment rules + scalar_block_layout.
// Keep host-side struct layout identical.
struct DrawRecord
{
    uint meshletIndex;
    uint materialIndex;
    uint vertexStrideBytes;
    uint flags;
    uint64_t vertexAddress;
    uint transformIndex;
    uint padding0;
    mat4 model;
};

struct Meshlet
{
    uint vertexOffset;
    uint vertexCount;
    uint triangleOffset;
    uint triangleCount;
    uint materialIndex;
    uint paddingU0;
    uint paddingU1;
    uint paddingU2;
    vec3 center;
    float radius;
    vec3 coneAxis;
    float coneCutoff;
    vec3 coneApex;
    float paddingF0;
};

// --------------------------------------------------------------------------
// GPU scene database / culling structs
// --------------------------------------------------------------------------

struct GpuInstance
{
    uint meshIndex;
    uint materialIndex;
    uint transformIndex;
    uint flags;
};

struct GpuMeshEntry
{
    uint meshletOffset;
    uint meshletCount;
    uint materialOffset;
    uint materialCount;

    uint vertexStrideBytes;
    uint vertexByteOffset;
    uint indexBase;
    uint flags;
};

struct GpuVisibleMeshlet
{
    uint meshletIndex;
    uint instanceIndex;
    uint materialIndex;
    uint flags;
};

struct DrawIndirectCommand
{
    uint count;
    uint instanceCount;
    uint first;
    uint firstInstance;
};

// --------------------------------------------------------------------------
// Optional resource declarations
// --------------------------------------------------------------------------

#ifdef VULTRA_DECLARE_CAMERA
layout(set = VULTRA_SCENE_SET, binding = VULTRA_CAMERA_BINDING) uniform Camera
{
    CameraData data;
} u_CameraBlock;
#define u_Camera u_CameraBlock.data
#endif

#ifdef VULTRA_DECLARE_DRAW_BUFFER_READONLY
layout(set = VULTRA_SCENE_SET, binding = VULTRA_DRAW_BINDING, std430) readonly buffer DrawBuffer
{
    DrawRecord draws[];
} s_Draws;
#endif

#ifdef VULTRA_DECLARE_DRAW_BUFFER_READWRITE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_DRAW_BINDING, std430) buffer DrawBuffer
{
    DrawRecord draws[];
} s_Draws;
#endif

#ifdef VULTRA_DECLARE_INSTANCE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_INSTANCE_BINDING, std430) readonly buffer InstanceBuffer
{
    GpuInstance instances[];
} s_Instances;
#endif

#ifdef VULTRA_DECLARE_MESH_TABLE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MESH_TABLE_BINDING, std430) readonly buffer MeshTableBuffer
{
    GpuMeshEntry meshes[];
} s_MeshTable;
#endif

#ifdef VULTRA_DECLARE_MESHLET_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MESHLET_BINDING, std430) readonly buffer MeshletBuffer
{
    Meshlet meshlets[];
} s_Meshlets;
#endif

#ifdef VULTRA_DECLARE_MODEL_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MODEL_BINDING, std430) readonly buffer ModelBuffer
{
    mat4 models[];
} s_Models;
#endif

#ifdef VULTRA_DECLARE_VISIBLE_MESHLET_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_VISIBLE_MESHLET_BINDING, std430) buffer VisibleMeshletBuffer
{
    GpuVisibleMeshlet visibleMeshlets[];
} s_VisibleMeshlets;
#endif

#ifdef VULTRA_DECLARE_VISIBLE_COUNT_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_VISIBLE_COUNT_BINDING, std430) buffer VisibleMeshletCountBuffer
{
    uint visibleCount;
} s_VisibleCount;
#endif

#ifdef VULTRA_DECLARE_MESHLET_VERTEX_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MESHLET_VERTEX_BINDING, std430) readonly buffer MeshletVertexBuffer
{
    uint meshletVertices[];
} s_MeshletVertices;
#endif

#ifdef VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MESHLET_TRIANGLE_BINDING, std430) readonly buffer MeshletTriangleBuffer
{
    uint meshletTriangles[];
} s_MeshletTriangles;
#endif

#ifdef VULTRA_DECLARE_INDIRECT_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_INDIRECT_BINDING, std430) buffer IndirectBuffer
{
    DrawIndirectCommand commands[];
} s_Indirect;
#endif

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

#ifdef VULTRA_DECLARE_MATERIAL_TABLE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MATERIAL_TABLE_BINDING, std430) readonly buffer MaterialTable
{
    MaterialEntry materials[];
} s_Materials;
#endif

#ifdef VULTRA_DECLARE_MATERIAL_PARAMS
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MATERIAL_PARAMS_BINDING, std430) readonly buffer MaterialParams
{
    uint words[]; // byte-addressed via 32-bit words
} s_MaterialParams;
#endif

#ifdef VULTRA_DECLARE_BINDLESS_TEXTURES
// Bindless texture array
layout(set = VULTRA_TEXTURE_SET, binding = VULTRA_BINDLESS_TEXTURES_BINDING) uniform sampler2D bindlessTextures[];
#define getBindlessTexture(idx) bindlessTextures[nonuniformEXT(idx)]
#endif

#ifdef VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
uint load_meshlet_triangle_index(uint triIndex)
{
    return s_MeshletTriangles.meshletTriangles[triIndex];
}
#endif

bool sphere_frustum_test(CameraData cam, vec3 centerWS, float radiusWS)
{
    for (uint i = 0u; i < 6u; ++i)
    {
        vec4 p = cam.frustumPlanes[i];
        float d = dot(p.xyz, centerWS) + p.w;
        if (d < -radiusWS)
            return false;
    }
    return true;
}

bool cone_backface_cull(vec3 coneApexWS, vec3 coneAxisWS, float coneCutoff, vec3 cameraPosWS)
{
    vec3 toCamera = normalize(cameraPosWS - coneApexWS);
    float d = dot(toCamera, normalize(coneAxisWS));
    return d >= coneCutoff;
}

float extract_max_scale(mat4 model)
{
    vec3 sx = vec3(model[0][0], model[0][1], model[0][2]);
    vec3 sy = vec3(model[1][0], model[1][1], model[1][2]);
    vec3 sz = vec3(model[2][0], model[2][1], model[2][2]);
    return max(length(sx), max(length(sy), length(sz)));
}

uint64_t make_u64(uint lo, uint hi)
{
    return (uint64_t(hi) << 32ul) | uint64_t(lo);
}

#ifdef VULTRA_DECLARE_MATERIAL_PARAMS

float _load_f32(uint w) { return uintBitsToFloat(w); }

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

#endif
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

#ifdef VULTRA_DECLARE_MATERIAL_TABLE

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

#endif

#endif // VULTRA_GPU_SCENE_GLSL
