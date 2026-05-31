[vshader]
language = glsl
version = 460

[vert]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MESHLET_VERTEX_BUFFER
#define VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
#define VULTRA_DECLARE_SKIN_MATRIX_BUFFER
#include "include/common/gpu_scene.glsl"
#include "include/common/bda_vertex.glsl"

layout(location = 0) out vec2 v_TexCoord0;
layout(location = 1) flat out uint v_DrawID;
layout(location = 2) flat out uint v_TriangleIndex;

layout(push_constant) uniform VisibilityPushConstants
{
    uint maxDraws;
    uint maxMeshlets;
    uint maxMeshletVertices;
    uint maxMeshletTriangles;
} u_PC;

void main()
{
    uint drawId = gl_BaseInstance;
    if (drawId >= u_PC.maxDraws)
    {
        v_TexCoord0 = vec2(0.0);
        v_DrawID = 0u;
        v_TriangleIndex = 0u;
        gl_Position = vec4(2.0, 2.0, 1.0, 1.0);
        return;
    }

    DrawRecord d = s_Draws.draws[drawId];
    if (d.primitiveIndex >= u_PC.maxMeshlets)
    {
        v_TexCoord0 = vec2(0.0);
        v_DrawID = 0u;
        v_TriangleIndex = 0u;
        gl_Position = vec4(2.0, 2.0, 1.0, 1.0);
        return;
    }

    Meshlet meshlet = s_Meshlets.meshlets[d.primitiveIndex];

    uint packedTriVertex = uint(gl_VertexIndex);
    uint triIndex = packedTriVertex / 3u;
    if (triIndex >= meshlet.triangleCount)
    {
        v_TexCoord0 = vec2(0.0);
        v_DrawID = 0u;
        v_TriangleIndex = 0u;
        gl_Position = vec4(2.0, 2.0, 1.0, 1.0);
        return;
    }

    uint corner = packedTriVertex % 3u;
    uint triDataIndex = meshlet.triangleOffset + triIndex * 3u + corner;
    if (triDataIndex >= u_PC.maxMeshletTriangles)
    {
        v_TexCoord0 = vec2(0.0);
        v_DrawID = 0u;
        v_TriangleIndex = 0u;
        gl_Position = vec4(2.0, 2.0, 1.0, 1.0);
        return;
    }

    uint localVertex = load_meshlet_triangle_index(triDataIndex);
    if (localVertex >= meshlet.vertexCount)
    {
        v_TexCoord0 = vec2(0.0);
        v_DrawID = 0u;
        v_TriangleIndex = 0u;
        gl_Position = vec4(2.0, 2.0, 1.0, 1.0);
        return;
    }

    uint meshletVertexIndex = meshlet.vertexOffset + localVertex;
    if (meshletVertexIndex >= u_PC.maxMeshletVertices)
    {
        v_TexCoord0 = vec2(0.0);
        v_DrawID = 0u;
        v_TriangleIndex = 0u;
        gl_Position = vec4(2.0, 2.0, 1.0, 1.0);
        return;
    }

    uint globalVertex = s_MeshletVertices.meshletVertices[meshletVertexIndex];

    Vertex v = load_vertex(d, globalVertex);

    v_TexCoord0 = v.texCoord0;

    vec4 worldPos4 = d.model * vtx_skin_matrix(d, v) * vec4(v.position, 1.0);

    v_DrawID = drawId;
    v_TriangleIndex = triIndex;
    gl_Position = u_Camera.viewProjection * vec4(worldPos4.xyz, 1.0);
}

[frag]
layout(location = 0) in vec2 v_TexCoord0;
layout(location = 1) flat in uint v_DrawID;
layout(location = 2) flat in uint v_TriangleIndex;

layout(location = 0) out uint VisibilityOutput;

void main()
{
	// Pack draw ID and meshlet-local triangle ID. Thin G-Buffer resolve uses this
	// to fetch all three vertices and reconstruct attributes once per visible pixel.
	VisibilityOutput = (v_DrawID << 16) | (v_TriangleIndex & 0xFFFFu);
}
