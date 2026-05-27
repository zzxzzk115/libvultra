[vshader]
language = glsl
version = 460

[vert]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MESHLET_VERTEX_BUFFER
#define VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
#include "include/common/gpu_scene.glsl"
#include "include/common/bda_vertex.glsl"

layout(location = 0) out vec3 v_Color;
layout(location = 1) out vec2 v_TexCoord0;
layout(location = 2) out vec2 v_TexCoord1;
layout(location = 3) out vec3 v_FragPos;
layout(location = 4) out mat3 v_TBN;

layout(location = 7) flat out uint v_MaterialIndex;
layout(location = 8) flat out uint v_MeshletIndex;
layout(location = 9) flat out uint v_TriangleIndex;
layout(location = 10) flat out uint v_VertexAttributeMask;

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

    Vertex v = load_vertex(d, globalVertex);

    v_Color = v.color;
    v_TexCoord0 = v.texCoord0;
    v_TexCoord1 = v.texCoord1;

    vec4 worldPos4 = d.model * vec4(v.position, 1.0);
    v_FragPos = worldPos4.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(d.model)));
    vec3 N = normalize(normalMatrix * v.normal);
    vec3 T = normalize(normalMatrix * v.tangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * v.tangent.w;
    v_TBN = mat3(T, B, N);

    v_MaterialIndex = d.materialIndex;
    v_MeshletIndex = d.primitiveIndex;
    v_TriangleIndex = triIndex;
    v_VertexAttributeMask = d.vertexAttributeMask;
    gl_Position = u_Camera.viewProjection * vec4(v_FragPos, 1.0);
}
