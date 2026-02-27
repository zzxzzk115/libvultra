#ifndef VULTRA_GPU_SCENE_GLSL
#define VULTRA_GPU_SCENE_GLSL

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

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

    // Per-draw transform
    mat4 model;

    uint materialIndex;       // index into MaterialTable
    uint flags;               // reserved
    uint padding0;
    uint padding1;
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

float _load_f32(uint w) { return uintBitsToFloat(w); }

vec4 load_vec4_bytes(uint baseByteOffset, uint byteOffset)
{
    uint addr = (baseByteOffset + byteOffset) >> 2u;
    return vec4(_load_f32(s_MaterialParams.words[addr + 0u]),
                _load_f32(s_MaterialParams.words[addr + 1u]),
                _load_f32(s_MaterialParams.words[addr + 2u]),
                _load_f32(s_MaterialParams.words[addr + 3u]));
}

// TestMaterialPass convention:
//   offset + 0 : vec4 mainColor (baseColor / diffuseColor / phong diffuse)
vec4 material_main_color(uint materialIndex)
{
    MaterialEntry m = s_Materials.materials[materialIndex];
    return load_vec4_bytes(m.blockOffsetBytes, 0u);
}

#endif // VULTRA_GPU_SCENE_GLSL
