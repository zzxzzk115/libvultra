[vshader]
language = glsl
version = 460

[comp]
#define VULTRA_DECLARE_DRAW_BUFFER_READWRITE
#define VULTRA_DECLARE_INSTANCE_BUFFER
#define VULTRA_DECLARE_MESH_TABLE_BUFFER
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MODEL_BUFFER
#define VULTRA_DECLARE_VISIBLE_MESHLET_BUFFER
#define VULTRA_DECLARE_VISIBLE_COUNT_BUFFER
#define VULTRA_DECLARE_INDIRECT_BUFFER
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform BuildPushConstants
{
    uint maxDraws;
    uint vertexAddressLo;
    uint vertexAddressHi;
    uint padding0;
} u_PC;

void main()
{
    uint drawId = gl_GlobalInvocationID.x;
    if (drawId >= u_PC.maxDraws)
        return;

    if (drawId >= s_VisibleCount.visibleCount)
    {
        s_Indirect.commands[drawId].count = 0u;
        s_Indirect.commands[drawId].instanceCount = 0u;
        s_Indirect.commands[drawId].first = 0u;
        s_Indirect.commands[drawId].firstInstance = 0u;
        return;
    }

    GpuVisibleMeshlet vis = s_VisibleMeshlets.visibleMeshlets[drawId];
    GpuInstance inst = s_Instances.instances[vis.instanceIndex];
    GpuMeshEntry mesh = s_MeshTable.meshes[inst.meshIndex];
    Meshlet meshlet = s_Meshlets.meshlets[vis.meshletIndex];

    DrawRecord dr;
    dr.primitiveIndex = vis.meshletIndex;
    dr.materialIndex = vis.materialIndex;
    dr.vertexStrideBytes = mesh.vertexStrideBytes;
    dr.flags = 0u;
    dr.vertexAddress = make_u64(u_PC.vertexAddressLo, u_PC.vertexAddressHi);
    dr.instanceIndex = vis.instanceIndex;
    dr.padding0 = 0u;
    dr.model = s_Models.models[inst.transformIndex];
    s_Draws.draws[drawId] = dr;

    DrawIndirectCommand cmd;
    cmd.count = meshlet.triangleCount * 3u;
    cmd.instanceCount = 1u;
    cmd.first = 0u;
    cmd.firstInstance = drawId;
    s_Indirect.commands[drawId] = cmd;
}
