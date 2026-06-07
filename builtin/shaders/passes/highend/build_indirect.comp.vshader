[vshader]
id       = "builtin/highend/build_indirect.comp"
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
#define VULTRA_DECLARE_MATERIAL_TABLE
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform BuildPushConstants
{
    uint maxDraws;
    uint vertexAddressLo;
    uint vertexAddressHi;
    uint maxVisibleMeshlets;
    uint maxMeshlets;
} u_PC;

void main()
{
    const uint kMeshletVisibleFlag = 1u;
    const uint kDrawFlagMeshlet = 1u;
    const uint kDrawQueueShift = 8u;

    uint drawId = gl_GlobalInvocationID.x;
    if (drawId >= u_PC.maxDraws)
        return;

    // Default to invalid each frame; DrawsetBuildPass rebuilds grouped command windows.
    s_Draws.draws[drawId].flags = 0u;

    uint visibleCount = min(s_VisibleCount.visibleCount, u_PC.maxVisibleMeshlets);
    if (drawId >= visibleCount)
        return;

    GpuVisibleMeshlet vis = s_VisibleMeshlets.visibleMeshlets[drawId];
    if ((vis.flags & kMeshletVisibleFlag) == 0u)
        return;
    if (vis.meshletIndex >= u_PC.maxMeshlets)
        return;

    GpuInstance inst = s_Instances.instances[vis.instanceIndex];
    GpuMeshEntry mesh = s_MeshTable.meshes[inst.meshIndex];
    Meshlet meshlet = s_Meshlets.meshlets[vis.meshletIndex];

    DrawRecord dr;
    dr.primitiveIndex = vis.meshletIndex;
    dr.materialIndex = vis.materialIndex;
    dr.vertexStrideBytes = mesh.vertexStrideBytes;
    uint renderQueue = get_material_render_queue(vis.materialIndex);
    dr.flags = kDrawFlagMeshlet | (renderQueue << kDrawQueueShift);
    dr.vertexAddress = make_u64(u_PC.vertexAddressLo, u_PC.vertexAddressHi);
    dr.instanceIndex = vis.instanceIndex;
    dr.vertexAttributeMask = mesh.vertexAttributeMask;
    dr.positionOffsetBytes = mesh.positionOffsetBytes;
    dr.normalOffsetBytes = mesh.normalOffsetBytes;
    dr.colorOffsetBytes = mesh.colorOffsetBytes;
    dr.texCoord0OffsetBytes = mesh.texCoord0OffsetBytes;
    dr.texCoord1OffsetBytes = mesh.texCoord1OffsetBytes;
    dr.tangentOffsetBytes = mesh.tangentOffsetBytes;
    dr.jointIndicesOffsetBytes = mesh.jointIndicesOffsetBytes;
    dr.jointWeightsOffsetBytes = mesh.jointWeightsOffsetBytes;
    dr.skinMatrixOffset = inst.skinMatrixOffset;
    dr.skinMatrixCount = inst.skinMatrixCount;
    dr.entityPickingId = inst.entityPickingId;
    dr.model = s_Models.models[inst.transformIndex];
    s_Draws.draws[drawId] = dr;
}
