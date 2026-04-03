[vshader]
language = glsl
version = 460

[keywords]
VTX_HAS_COLOR : bool permute
VTX_HAS_NORMAL : bool permute
VTX_HAS_UV0 : bool permute
VTX_HAS_UV1 : bool permute
VTX_HAS_TANGENT : bool permute

[vert]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MESHLET_VERTEX_BUFFER
#define VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
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

#if VTX_HAS_COLOR
layout(location = 0) out vec3 v_Color;
#endif

#if VTX_HAS_UV0
layout(location = 1) out vec2 v_TexCoord0;
#endif

#if VTX_HAS_UV1
layout(location = 2) out vec2 v_TexCoord1;
#endif

layout(location = 3) out vec3 v_FragPos;

#if VTX_HAS_TANGENT && VTX_HAS_NORMAL
layout(location = 4) out mat3 v_TBN;
#elif VTX_HAS_NORMAL
layout(location = 4) out vec3 v_Normal;
#endif

layout(location = 7) flat out uint v_MaterialIndex;
layout(location = 8) flat out uint v_MeshletIndex;
layout(location = 9) flat out uint v_TriangleIndex;

void main()
{
    uint drawId = gl_BaseInstance;
    DrawRecord d = s_Draws.draws[drawId];
    Meshlet meshlet = s_Meshlets.meshlets[d.primitiveIndex];

    uint packedTriVertex = uint(gl_VertexIndex);
    uint triIndex = packedTriVertex / 3u;
    uint corner = packedTriVertex % 3u;
    uint triDataIndex = meshlet.triangleOffset + triIndex * 3u + corner;
    uint localVertex = load_meshlet_triangle_index(triDataIndex);
    uint globalVertex = s_MeshletVertices.meshletVertices[meshlet.vertexOffset + localVertex];

    VertexBuffer vb = VertexBuffer(d.vertexAddress);
    Vertex v = vb.vertices[globalVertex];

#if VTX_HAS_COLOR
    v_Color = v.color;
#endif
#if VTX_HAS_UV0
    v_TexCoord0 = v.texCoord0;
#endif
#if VTX_HAS_UV1
    v_TexCoord1 = v.texCoord1;
#endif

    vec4 worldPos4 = d.model * vec4(v.position, 1.0);
    v_FragPos = worldPos4.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(d.model)));
#if VTX_HAS_TANGENT && VTX_HAS_NORMAL
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
    v_MeshletIndex = d.primitiveIndex;
    v_TriangleIndex = triIndex;
    gl_Position = u_Camera.viewProjection * vec4(v_FragPos, 1.0);
}
