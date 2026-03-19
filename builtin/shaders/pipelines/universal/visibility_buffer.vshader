[vshader]
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute

[vert]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MESHLET_VERTEX_BUFFER
#define VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
#include "include/common/gpu_scene.glsl"

#if VTX_HAS_UV0
layout(location = 0) out vec2 v_TexCoord0;
#else
	#error "VTX_HAS_UV0 must be defined for visibility buffer shader"
#endif
layout(location = 1) flat out uint v_DrawID;
layout(location = 2) flat out uint v_TriangleDataIndex;

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

#if VTX_HAS_UV0
    v_TexCoord0 = v.texCoord0;
#else
	#error "VTX_HAS_UV0 must be defined for visibility buffer shader"
#endif

    vec4 worldPos4 = d.model * vec4(v.position, 1.0);

    v_DrawID = drawId;
	v_TriangleDataIndex = triDataIndex;
    gl_Position = u_Camera.viewProjection * vec4(worldPos4.xyz, 1.0);
}

[frag]
#if VTX_HAS_UV0
layout(location = 0) in vec2 v_TexCoord0;
#else
	#error "VTX_HAS_UV0 must be defined for visibility buffer shader"
#endif
layout(location = 1) flat in uint v_DrawID;
layout(location = 2) flat in uint v_TriangleDataIndex;

layout(location = 0) out uint VisibilityOutput;

void main()
{
	// Output the draw ID and triangle data index for use in visibility buffer techniques.
	// The triangle data index can be used to fetch vertex indices for computing derivatives or other triangle-specific data.
	VisibilityOutput = (v_DrawID << 16) | (v_TriangleDataIndex & 0xFFFFu);
}