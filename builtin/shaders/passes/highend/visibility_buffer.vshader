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

layout(location = 0) out vec2 v_TexCoord0;
layout(location = 1) flat out uint v_DrawID;
layout(location = 2) flat out uint v_TriangleIndex;

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

    v_TexCoord0 = v.texCoord0;

    vec4 worldPos4 = d.model * vec4(v.position, 1.0);

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
