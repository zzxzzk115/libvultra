#ifndef VULTRA_BDA_VERTEX_GLSL
#define VULTRA_BDA_VERTEX_GLSL

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// ============================================================================
// bda_vertex.glsl
//
// Buffer-Device-Address vertex pulling helpers.
//
// NOTE:
//   Vertex layout is an engine contract. Keep this struct aligned with the
//   packing performed by the mesh uploader / vertex packer.
//
// Baseline (legacy-compatible):
//   position : vec3
//   color    : vec3
//   normal   : vec3
//   texCoord : vec2
//   tangent  : vec4  (xyz + handedness w)
// ============================================================================

struct Vertex
{
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord0;
    vec4 tangent;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer
{
    Vertex vertices[];
};

layout(buffer_reference, scalar) readonly buffer IndexBuffer
{
    uint indices[];
};

#endif // VULTRA_BDA_VERTEX_GLSL
