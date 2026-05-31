#ifndef VULTRA_BDA_VERTEX_GLSL
#define VULTRA_BDA_VERTEX_GLSL

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

const uint VULTRA_VERTEX_ATTR_POSITION = 1u << 0u;
const uint VULTRA_VERTEX_ATTR_NORMAL = 1u << 1u;
const uint VULTRA_VERTEX_ATTR_COLOR = 1u << 2u;
const uint VULTRA_VERTEX_ATTR_UV0 = 1u << 3u;
const uint VULTRA_VERTEX_ATTR_UV1 = 1u << 4u;
const uint VULTRA_VERTEX_ATTR_TANGENT = 1u << 5u;
const uint VULTRA_INVALID_VERTEX_OFFSET = 0xFFFFFFFFu;

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer FloatBuffer
{
    float values[];
};

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer IntBuffer
{
    int values[];
};

struct Vertex
{
    vec3 position;
    vec3 normal;
    vec3 color;
    vec2 texCoord0;
    vec2 texCoord1;
    vec4 tangent;
    ivec4 jointIndices;
    vec4 jointWeights;
};

bool vertex_has_attribute(uint attributeMask, uint flag)
{
    return (attributeMask & flag) != 0u;
}

float load_vertex_f32(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, float fallback)
{
    if (offsetBytes == VULTRA_INVALID_VERTEX_OFFSET)
        return fallback;
    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return data.values[0];
}

vec2 load_vertex_vec2(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec2 fallback)
{
    if (offsetBytes == VULTRA_INVALID_VERTEX_OFFSET)
        return fallback;
    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec2(data.values[0], data.values[1]);
}

vec3 load_vertex_vec3(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec3 fallback)
{
    if (offsetBytes == VULTRA_INVALID_VERTEX_OFFSET)
        return fallback;
    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec3(data.values[0], data.values[1], data.values[2]);
}

vec4 load_vertex_vec4(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec4 fallback)
{
    if (offsetBytes == VULTRA_INVALID_VERTEX_OFFSET)
        return fallback;
    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec4(data.values[0], data.values[1], data.values[2], data.values[3]);
}

ivec4 load_vertex_ivec4(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, ivec4 fallback)
{
    if (offsetBytes == VULTRA_INVALID_VERTEX_OFFSET)
        return fallback;
    IntBuffer data = IntBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return ivec4(data.values[0], data.values[1], data.values[2], data.values[3]);
}

Vertex load_vertex(in DrawRecord d, uint vertexIndex)
{
    Vertex v;
    v.position = load_vertex_vec3(d.vertexAddress,
                                  vertexIndex,
                                  d.vertexStrideBytes,
                                  d.positionOffsetBytes,
                                  vec3(0.0));
    v.normal = load_vertex_vec3(d.vertexAddress,
                                vertexIndex,
                                d.vertexStrideBytes,
                                d.normalOffsetBytes,
                                vec3(0.0, 1.0, 0.0));
    v.color = load_vertex_vec3(d.vertexAddress,
                               vertexIndex,
                               d.vertexStrideBytes,
                               d.colorOffsetBytes,
                               vec3(1.0));
    v.texCoord0 = load_vertex_vec2(d.vertexAddress,
                                   vertexIndex,
                                   d.vertexStrideBytes,
                                   d.texCoord0OffsetBytes,
                                   vec2(0.0));
    v.texCoord1 = load_vertex_vec2(d.vertexAddress,
                                   vertexIndex,
                                   d.vertexStrideBytes,
                                   d.texCoord1OffsetBytes,
                                   vec2(0.0));
    v.tangent = load_vertex_vec4(d.vertexAddress,
                                 vertexIndex,
                                 d.vertexStrideBytes,
                                 d.tangentOffsetBytes,
                                 vec4(1.0, 0.0, 0.0, 1.0));
    v.jointIndices = load_vertex_ivec4(d.vertexAddress,
                                       vertexIndex,
                                       d.vertexStrideBytes,
                                       d.jointIndicesOffsetBytes,
                                       ivec4(0));
    v.jointWeights = load_vertex_vec4(d.vertexAddress,
                                      vertexIndex,
                                      d.vertexStrideBytes,
                                      d.jointWeightsOffsetBytes,
                                      vec4(0.0));
    return v;
}

bool vtx_has_skin(in DrawRecord d)
{
    return d.skinMatrixOffset != 0xFFFFFFFFu &&
           d.skinMatrixCount > 0u &&
           d.jointIndicesOffsetBytes != VULTRA_INVALID_VERTEX_OFFSET &&
           d.jointWeightsOffsetBytes != VULTRA_INVALID_VERTEX_OFFSET;
}

mat4 vtx_skin_matrix(in DrawRecord d, in Vertex v)
{
#ifdef VULTRA_DECLARE_SKIN_MATRIX_BUFFER
    if (!vtx_has_skin(d))
        return mat4(1.0);

    mat4 skin = mat4(0.0);
    for (uint i = 0u; i < 4u; ++i)
    {
        int joint = v.jointIndices[int(i)];
        float weight = v.jointWeights[int(i)];
        if (joint >= 0 && weight > 0.0)
        {
            uint jointIndex = uint(joint);
            if (jointIndex < d.skinMatrixCount)
                skin += s_SkinMatrices.skinMatrices[d.skinMatrixOffset + jointIndex] * weight;
        }
    }
    return skin;
#else
    return mat4(1.0);
#endif
}

vec3 vtx_color(in Vertex v)
{
    return v.color;
}

vec3 vtx_normal(in Vertex v)
{
    return v.normal;
}

vec2 vtx_uv0(in Vertex v)
{
    return v.texCoord0;
}

vec4 vtx_tangent(in Vertex v)
{
    return v.tangent;
}

#endif // VULTRA_BDA_VERTEX_GLSL
