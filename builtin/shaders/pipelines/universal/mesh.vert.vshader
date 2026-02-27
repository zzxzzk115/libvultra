#version 460

#include "include/common/gpu_scene.glsl"

// ============================================================================
// mesh.vert.vshader
//
// Built-in GPU-driven mesh vertex shader.
//
// - Multi-draw indirect: gl_DrawID selects DrawRecord
// - Vertex pulling: buffer device address for vertex/index buffers
// - Legacy-compatible TBN construction (Gram-Schmidt + handedness)
//
// ============================================================================

#pragma keyword permute VTX_HAS_COLOR=0|1
#pragma keyword permute VTX_HAS_NORMAL=0|1
#pragma keyword permute VTX_HAS_UV0=0|1
#pragma keyword permute VTX_HAS_UV1=0|1
#pragma keyword permute VTX_HAS_TANGENT=0|1

#if VTX_HAS_COLOR
layout(location = 0) out vec3 v_Color;
#endif

#if VTX_HAS_UV0
layout(location = 1) out vec2 v_TexCoord;
#endif

layout(location = 2) out vec3 v_FragPos;

#if VTX_HAS_TANGENT
layout(location = 3) out mat3 v_TBN;
#elif VTX_HAS_NORMAL
layout(location = 3) out vec3 v_Normal;
#endif

layout(location = 6) flat out uint v_MaterialIndex;

void main()
{
    DrawRecord d = s_Draws.draws[gl_DrawID];

    VertexBuffer vb = VertexBuffer(d.vertexAddress);
    IndexBuffer  ib = IndexBuffer(d.indexAddress);

    uint   idx = ib.indices[uint(gl_VertexIndex)];
    Vertex v   = vb.vertices[idx];

    v_Color    = v.color;
    v_TexCoord = v.texCoord0;

    vec4 worldPos4 = d.model * vec4(v.position, 1.0);
    v_FragPos      = worldPos4.xyz;

    // Legacy-compatible normal matrix
    mat3 normalMatrix = transpose(inverse(mat3(d.model)));

#if VTX_HAS_TANGENT
    vec3 T = normalize(normalMatrix * v.tangent.xyz);
    vec3 N = normalize(normalMatrix * v.normal);

    // Gram-Schmidt orthogonalize
    T = normalize(T - dot(T, N) * N);

    vec3 B = cross(N, T) * v.tangent.w;

    v_TBN = mat3(T, B, N);
#elif VTX_HAS_NORMAL
    v_Normal = normalize(normalMatrix * v.normal);
#endif

    v_MaterialIndex = d.materialIndex;

    gl_Position = u_Camera.viewProj * vec4(v_FragPos, 1.0);
}
