#ifndef GLOBAL_MESHLET_DATA_GLSL
#define GLOBAL_MESHLET_DATA_GLSL

layout(push_constant) uniform GlobalMeshletDataPushConstants {
	uint64_t vertexBufferAddress;
    uint64_t meshletBufferAddress;
    uint64_t meshletVertexBufferAddress;
    uint64_t meshletTriangleBufferAddress;

    uint meshletCount;
	uint enableNormalMapping;
	uint debugMode; // 0: meshlet ID, 1: material index
	uint padding1;

    mat4 modelMatrix;
} g_Mesh;

#endif // GLOBAL_MESHLET_DATA_GLSL