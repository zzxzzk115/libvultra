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
// Vertex layout is an engine contract, but that contract usually has variants.
// Use compile-time macros to match the exact packing performed by the uploader.
// ============================================================================

#ifndef VTX_HAS_COLOR
#define VTX_HAS_COLOR 1
#endif

#ifndef VTX_HAS_NORMAL
#define VTX_HAS_NORMAL 1
#endif

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 1
#endif

#ifndef VTX_HAS_UV1
#define VTX_HAS_UV1 1
#endif

#ifndef VTX_HAS_TANGENT
#define VTX_HAS_TANGENT 1
#endif

// Optional: index width
#ifndef INDEX_U16
#define INDEX_U16 0
#endif

// ---- Vertex struct matches CPU packing for the selected variant ----
// IMPORTANT: scalar layout packs vec3 tightly (12 bytes), so CPU side must match.
struct Vertex
{
    vec3 position;

#if VTX_HAS_COLOR
    vec3 color;
#endif

#if VTX_HAS_NORMAL
    vec3 normal;
#endif

#if VTX_HAS_UV0
    vec2 texCoord0;
#endif

#if VTX_HAS_UV1
    vec2 texCoord1;
#endif

#if VTX_HAS_TANGENT
    vec4 tangent; // xyz + handedness w
#endif
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer
{
    Vertex vertices[];
};

// ---- Index buffers (16/32-bit) ----
#if INDEX_U16
layout(buffer_reference, scalar) readonly buffer IndexBuffer
{
    uint16_t indices[];
};
#else
layout(buffer_reference, scalar) readonly buffer IndexBuffer
{
    uint indices[];
};
#endif

// ---- Accessors: shader code can be mostly layout-agnostic ----
vec3 vtx_color(in Vertex v)
{
#if VTX_HAS_COLOR
    return v.color;
#else
    return vec3(1.0);
#endif
}

vec3 vtx_normal(in Vertex v)
{
#if VTX_HAS_NORMAL
    return v.normal;
#else
    return vec3(0.0, 1.0, 0.0);
#endif
}

vec2 vtx_uv0(in Vertex v)
{
#if VTX_HAS_UV0
    return v.texCoord0;
#else
    return vec2(0.0);
#endif
}

vec4 vtx_tangent(in Vertex v)
{
#if VTX_HAS_TANGENT
    return v.tangent;
#else
    // default tangent points +X, handedness +1
    return vec4(1.0, 0.0, 0.0, 1.0);
#endif
}

#endif // VULTRA_BDA_VERTEX_GLSL