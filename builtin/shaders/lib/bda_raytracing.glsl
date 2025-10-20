#ifndef BDA_RAYTRACING_GLSL
#define BDA_RAYTRACING_GLSL

#include "lib/bda_vertex.glsl"

Vertex fromBufferDeviceAddresses(uint64_t vertexBufferAddress, uint64_t indexBufferAddress, int v)
{
	VertexBuffer vb = VertexBuffer(vertexBufferAddress);
    IndexBuffer ib  = IndexBuffer(indexBufferAddress);

	return vb.vertices[ib.indices[gl_PrimitiveID * 3 + v]];
}

#endif // BDA_RAYTRACING_GLSL