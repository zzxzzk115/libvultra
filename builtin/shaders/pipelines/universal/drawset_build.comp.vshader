[vshader]
language = glsl
version = 460

[comp]
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_INDIRECT_BUFFER
#define VULTRA_DECLARE_DRAW_SET_BUFFER
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform DrawsetBuildPushConstants
{
    uint maxDraws;
    uint padding0;
    uint padding1;
    uint padding2;
} u_PC;

void main()
{
    const uint kDrawFlagMeshlet = 1u;
    const uint kDrawQueueShift  = 8u;
    const uint kQueueCount      = VULTRA_RENDER_QUEUE_COUNT;

    if (gl_GlobalInvocationID.x != 0u)
        return;

    for (uint i = 0u; i < 8u; ++i)
        s_DrawSets.drawSetCounts[i] = 0u;

    for (uint q = 0u; q < kQueueCount; ++q)
    {
        for (uint i = 0u; i < u_PC.maxDraws; ++i)
        {
            uint cmdIndex = q * u_PC.maxDraws + i;
            s_Indirect.commands[cmdIndex].count = 0u;
            s_Indirect.commands[cmdIndex].instanceCount = 0u;
            s_Indirect.commands[cmdIndex].first = 0u;
            s_Indirect.commands[cmdIndex].firstInstance = 0u;
        }
    }

    for (uint drawId = 0u; drawId < u_PC.maxDraws; ++drawId)
    {
        DrawRecord dr = s_Draws.draws[drawId];
        if ((dr.flags & kDrawFlagMeshlet) == 0u)
            continue;

        uint queueId = (dr.flags >> kDrawQueueShift) & 0xFFu;
        if (queueId >= kQueueCount)
            queueId = VULTRA_RENDER_QUEUE_OPAQUE;

        uint queueSlot = s_DrawSets.drawSetCounts[queueId];
        if (queueSlot >= u_PC.maxDraws)
            continue;

        Meshlet meshlet = s_Meshlets.meshlets[dr.primitiveIndex];

        DrawIndirectCommand cmd;
        cmd.count = meshlet.triangleCount * 3u;
        cmd.instanceCount = 1u;
        cmd.first = 0u;
        cmd.firstInstance = drawId;

        s_Indirect.commands[queueId * u_PC.maxDraws + queueSlot] = cmd;
        s_DrawSets.drawSetCounts[queueId] = queueSlot + 1u;
    }
}
