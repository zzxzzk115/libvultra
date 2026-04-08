#ifndef VULTRA_MESH_SCENE_GLSL
#define VULTRA_MESH_SCENE_GLSL

#if defined(VULTRA_DECLARE_DRAW_BUFFER_READONLY) || defined(VULTRA_DECLARE_DRAW_BUFFER_READWRITE)
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#endif

#ifndef VULTRA_DRAW_BINDING
#define VULTRA_DRAW_BINDING 1
#endif

#ifndef VULTRA_INSTANCE_BINDING
#define VULTRA_INSTANCE_BINDING 2
#endif

#ifndef VULTRA_MESH_TABLE_BINDING
#define VULTRA_MESH_TABLE_BINDING 3
#endif

#ifndef VULTRA_MESHLET_BINDING
#define VULTRA_MESHLET_BINDING 4
#endif

#ifndef VULTRA_MODEL_BINDING
#define VULTRA_MODEL_BINDING 5
#endif

#ifndef VULTRA_VISIBLE_MESHLET_BINDING
#define VULTRA_VISIBLE_MESHLET_BINDING 6
#endif

#ifndef VULTRA_VISIBLE_COUNT_BINDING
#define VULTRA_VISIBLE_COUNT_BINDING 7
#endif

#ifndef VULTRA_MESHLET_VERTEX_BINDING
#define VULTRA_MESHLET_VERTEX_BINDING 10
#endif

#ifndef VULTRA_MESHLET_TRIANGLE_BINDING
#define VULTRA_MESHLET_TRIANGLE_BINDING 11
#endif

#ifndef VULTRA_INDIRECT_BINDING
#define VULTRA_INDIRECT_BINDING 12
#endif

#ifndef VULTRA_VISIBLE_INSTANCE_BINDING
#define VULTRA_VISIBLE_INSTANCE_BINDING 24
#endif

#ifndef VULTRA_VISIBLE_INSTANCE_COUNT_BINDING
#define VULTRA_VISIBLE_INSTANCE_COUNT_BINDING 25
#endif

#ifndef VULTRA_DISPATCH_ARGS_BINDING
#define VULTRA_DISPATCH_ARGS_BINDING 26
#endif

#ifndef VULTRA_DRAW_SET_BINDING
#define VULTRA_DRAW_SET_BINDING 30
#endif

#if defined(VULTRA_DECLARE_DRAW_BUFFER_READONLY) || defined(VULTRA_DECLARE_DRAW_BUFFER_READWRITE)
struct DrawRecord
{
    uint primitiveIndex;
    uint materialIndex;
    uint vertexStrideBytes;
    uint flags;
    uint64_t vertexAddress;
    uint instanceIndex;
    uint padding0;
    mat4 model;
};

uint64_t make_u64(uint lo, uint hi)
{
    return (uint64_t(hi) << 32ul) | uint64_t(lo);
}
#endif

#if defined(VULTRA_DECLARE_MESHLET_BUFFER)
struct Meshlet
{
    uint vertexOffset;
    uint vertexCount;
    uint triangleOffset;
    uint triangleCount;
    uint materialIndex;
    uint paddingU0;
    uint paddingU1;
    uint paddingU2;
    vec3 center;
    float radius;
    vec3 coneAxis;
    float coneCutoff;
    vec3 coneApex;
    float paddingF0;
};
#endif

#if defined(VULTRA_DECLARE_INSTANCE_BUFFER)
struct GpuInstance
{
    uint meshIndex;
    uint materialIndex;
    uint transformIndex;
    uint flags;
};
#endif

#if defined(VULTRA_DECLARE_MESH_TABLE_BUFFER)
struct GpuMeshEntry
{
    uint meshletOffset;
    uint meshletCount;
    uint materialOffset;
    uint materialCount;

    uint vertexStrideBytes;
    uint vertexByteOffset;
    uint indexBase;
    uint flags;

    vec3 boundsCenter;
    float boundsRadius;
};
#endif

#if defined(VULTRA_DECLARE_VISIBLE_MESHLET_BUFFER)
struct GpuVisibleMeshlet
{
    uint meshletIndex;
    uint instanceIndex;
    uint materialIndex;
    uint flags;
};
#endif

struct DrawIndirectCommand
{
    uint count;
    uint instanceCount;
    uint first;
    uint firstInstance;
};

struct DrawIndexedIndirectCommand
{
    uint count;
    uint instanceCount;
    uint first;
    int  vertexOffset;
    uint firstInstance;
};

#ifdef VULTRA_DECLARE_DRAW_BUFFER_READONLY
layout(set = VULTRA_SCENE_SET, binding = VULTRA_DRAW_BINDING, std430) readonly buffer DrawBuffer
{
    DrawRecord draws[];
} s_Draws;
#endif

#ifdef VULTRA_DECLARE_DRAW_BUFFER_READWRITE
layout(set = VULTRA_SCENE_SET, binding = VULTRA_DRAW_BINDING, std430) buffer DrawBuffer
{
    DrawRecord draws[];
} s_Draws;
#endif

#ifdef VULTRA_DECLARE_INSTANCE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_INSTANCE_BINDING, std430) readonly buffer InstanceBuffer
{
    GpuInstance instances[];
} s_Instances;
#endif

#ifdef VULTRA_DECLARE_MESH_TABLE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MESH_TABLE_BINDING, std430) readonly buffer MeshTableBuffer
{
    GpuMeshEntry meshes[];
} s_MeshTable;
#endif

#ifdef VULTRA_DECLARE_MESHLET_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MESHLET_BINDING, std430) readonly buffer MeshletBuffer
{
    Meshlet meshlets[];
} s_Meshlets;
#endif

#ifdef VULTRA_DECLARE_MODEL_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MODEL_BINDING, std430) readonly buffer ModelBuffer
{
    mat4 models[];
} s_Models;
#endif

#ifdef VULTRA_DECLARE_VISIBLE_MESHLET_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_VISIBLE_MESHLET_BINDING, std430) buffer VisibleMeshletBuffer
{
    GpuVisibleMeshlet visibleMeshlets[];
} s_VisibleMeshlets;
#endif

#ifdef VULTRA_DECLARE_VISIBLE_COUNT_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_VISIBLE_COUNT_BINDING, std430) buffer VisibleMeshletCountBuffer
{
    uint visibleCount;
} s_VisibleCount;
#endif

#ifdef VULTRA_DECLARE_VISIBLE_INSTANCE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_VISIBLE_INSTANCE_BINDING, std430) buffer VisibleInstanceBuffer
{
    uint instanceIndices[];
} s_VisibleInstances;
#endif

#ifdef VULTRA_DECLARE_VISIBLE_INSTANCE_COUNT_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_VISIBLE_INSTANCE_COUNT_BINDING, std430)
buffer VisibleInstanceCountBuffer
{
    uint visibleInstanceCount;
} s_VisibleInstanceCount;
#endif

#ifdef VULTRA_DECLARE_DISPATCH_ARGS_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_DISPATCH_ARGS_BINDING, std430) buffer DispatchArgsBuffer
{
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
} s_DispatchArgs;
#endif

#ifdef VULTRA_DECLARE_MESHLET_VERTEX_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MESHLET_VERTEX_BINDING, std430) readonly buffer MeshletVertexBuffer
{
    uint meshletVertices[];
} s_MeshletVertices;
#endif

#ifdef VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_MESHLET_TRIANGLE_BINDING, std430) readonly buffer MeshletTriangleBuffer
{
    uint meshletTriangles[];
} s_MeshletTriangles;
#endif

#ifdef VULTRA_DECLARE_INDIRECT_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_INDIRECT_BINDING, std430) buffer IndirectBuffer
{
    DrawIndirectCommand commands[];
} s_Indirect;
#endif

#ifdef VULTRA_DECLARE_DRAW_SET_BUFFER
layout(set = VULTRA_SCENE_SET, binding = VULTRA_DRAW_SET_BINDING, std430) buffer DrawSetBuffer
{
    uint drawSetCounts[8];
} s_DrawSets;
#endif

#ifdef VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
uint load_meshlet_triangle_index(uint triIndex)
{
    return s_MeshletTriangles.meshletTriangles[triIndex];
}
#endif

#endif
