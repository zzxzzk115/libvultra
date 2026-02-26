#ifndef BDA_VERTEX_GLSL
#define BDA_VERTEX_GLSL

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord;
    vec4 tangent;
};

layout(buffer_reference, scalar) buffer VertexBuffer { Vertex vertices[]; };
layout(buffer_reference, scalar) buffer IndexBuffer  { uint indices[]; };

#endif