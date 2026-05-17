#ifndef VULTRA_GPU_SCENE_GLSL
#define VULTRA_GPU_SCENE_GLSL

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

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

#ifndef VULTRA_STEREO_CAMERA_BINDING
#define VULTRA_STEREO_CAMERA_BINDING 23
#endif

#ifndef VULTRA_MATERIAL_TABLE_BINDING
#define VULTRA_MATERIAL_TABLE_BINDING 8
#endif

#ifndef VULTRA_MATERIAL_PARAMS_BINDING
#define VULTRA_MATERIAL_PARAMS_BINDING 9
#endif

#ifndef VULTRA_DEPTH_TEXTURE_BINDING
#define VULTRA_DEPTH_TEXTURE_BINDING 27
#endif

#ifndef VULTRA_HZB_TEXTURE_BINDING
#define VULTRA_HZB_TEXTURE_BINDING 28
#endif

#ifndef VULTRA_HZB_STORAGE_BINDING
#define VULTRA_HZB_STORAGE_BINDING 29
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

#if defined(VULTRA_DECLARE_DRAW_BUFFER_READONLY) || defined(VULTRA_DECLARE_DRAW_BUFFER_READWRITE) || \
    defined(VULTRA_DECLARE_INSTANCE_BUFFER) || defined(VULTRA_DECLARE_MESH_TABLE_BUFFER) || \
    defined(VULTRA_DECLARE_MESHLET_BUFFER) || defined(VULTRA_DECLARE_MODEL_BUFFER) || \
    defined(VULTRA_DECLARE_VISIBLE_MESHLET_BUFFER) || defined(VULTRA_DECLARE_VISIBLE_COUNT_BUFFER) || \
    defined(VULTRA_DECLARE_VISIBLE_INSTANCE_BUFFER) || defined(VULTRA_DECLARE_VISIBLE_INSTANCE_COUNT_BUFFER) || \
    defined(VULTRA_DECLARE_DISPATCH_ARGS_BUFFER) || defined(VULTRA_DECLARE_MESHLET_VERTEX_BUFFER) || \
    defined(VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER) || defined(VULTRA_DECLARE_INDIRECT_BUFFER) || \
    defined(VULTRA_DECLARE_DRAW_SET_BUFFER)
#include "mesh_scene.glsl"
#endif

#if defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_DRAW_BUFFER) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_PACKED_SOURCE_BUFFER) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BUFFER_READONLY) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_SPLAT_BUFFER_READWRITE) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BUFFER_READONLY) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_KEY_BUFFER_READWRITE) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BUFFER_READONLY) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_INDEX_BUFFER_READWRITE) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BUFFER) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_DISPATCH_ARGS_BUFFER) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_INDIRECT_BUFFER) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SORT_STORAGE_BUFFER) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SH_BUFFER) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BUFFER) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_SELECTED_SOURCE_BUFFER_READWRITE) || \
    defined(VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_FOVEATED_LAYER_BUFFERS)
#include "gaussian_splat_scene.glsl"
#endif

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

#ifdef VULTRA_DECLARE_STEREO_CAMERA
layout(set = VULTRA_SCENE_SET, binding = VULTRA_STEREO_CAMERA_BINDING) uniform StereoCamera
{
    CameraData cameras[2];
} u_StereoCameraBlock;
#endif

#ifdef VULTRA_DECLARE_DEPTH_TEXTURE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_DEPTH_TEXTURE_BINDING) uniform sampler2D u_DepthTexture;
#endif

#ifdef VULTRA_DECLARE_HZB_TEXTURE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_HZB_TEXTURE_BINDING) uniform sampler2D u_HzbTexture;
#endif

#ifdef VULTRA_DECLARE_HZB_STORAGE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_HZB_STORAGE_BINDING, r32f) uniform writeonly image2D u_HzbStorage;
#endif

#define VULTRA_MAT_INVALID 0u
#define VULTRA_MAT_PBRMR   1u
#define VULTRA_MAT_PBRSG   2u
#define VULTRA_MAT_UNLIT   3u
#define VULTRA_MAT_PHONG   4u

// Render queue IDs shared between GPU build passes and CPU-side inspection.
#define VULTRA_RENDER_QUEUE_OPAQUE      0u
#define VULTRA_RENDER_QUEUE_ALPHA_MASK  1u
#define VULTRA_RENDER_QUEUE_TRANSPARENT 2u
#define VULTRA_RENDER_QUEUE_POST        3u
#define VULTRA_RENDER_QUEUE_UI          4u
#define VULTRA_RENDER_QUEUE_COUNT       5u

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

#ifdef VULTRA_DECLARE_CAMERA
bool sphere_frustum_test(vec3 centerWS, float radiusWS)
{
    for (uint i = 0u; i < 6u; ++i)
    {
        vec4 p = u_Camera.frustumPlanes[i];
        float d = dot(p.xyz, centerWS) + p.w;
        if (d < -radiusWS)
            return false;
    }
    return true;
}
#else
bool sphere_frustum_test(vec3 centerWS, float radiusWS)
{
    return true;
}
#endif

// Alternative cone culling method inspired by Alan Wake 2 tech talk
bool cone_visible_alanwake2(
    vec3 coneAxisWS,
    float coneCutoff,
    vec3 centerWS,
    float radiusWS,
    vec3 cameraPosWS)
{
    vec3 toCenter = centerWS - cameraPosWS;
    float distToCenter = length(toCenter);

    if (distToCenter <= 1e-6)
        return true;

    float cutoff = coneCutoff + distToCenter + radiusWS;
    return dot(toCenter, coneAxisWS) < cutoff;
}

float extract_max_scale(mat4 model)
{
    vec3 sx = vec3(model[0][0], model[0][1], model[0][2]);
    vec3 sy = vec3(model[1][0], model[1][1], model[1][2]);
    vec3 sz = vec3(model[2][0], model[2][1], model[2][2]);
    return max(length(sx), max(length(sy), length(sz)));
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

uint get_material_render_queue(uint materialIndex)
{
    // Queue is encoded in MaterialEntry.reserved low byte.
    // TODO(vk-queue): formalize queue flags in cooked material metadata.
    uint queueId = s_Materials.materials[materialIndex].reserved & 0xFFu;
    return queueId < VULTRA_RENDER_QUEUE_COUNT ? queueId : VULTRA_RENDER_QUEUE_OPAQUE;
}

#ifdef VULTRA_DECLARE_MATERIAL_PARAMS

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

#endif

#endif // VULTRA_GPU_SCENE_GLSL
