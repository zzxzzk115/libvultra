[vshader]
language = glsl
version = 460

[keywords]
USE_FOVEATED_LAYER_OUTPUT : bool permute

[comp]
#if USE_FOVEATED_LAYER_OUTPUT
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_FOVEATED_LAYER_BUFFERS
#else
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BUFFER
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_INDIRECT_BUFFER
#endif
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void writeIndirectCommand(inout GeneralGaussianSplatIndirectCommand command, const uint instanceCount)
{
    command.count = 4u;
    command.instanceCount = instanceCount;
    command.first = 0u;
    command.firstInstance = 0u;
}

void main()
{
#if USE_FOVEATED_LAYER_OUTPUT
    writeIndirectCommand(s_GeneralGaussianSplatFoveatedFoveaIndirect.command,
                         s_GeneralGaussianSplatFoveatedFoveaVisibleCount.visibleCount);
    writeIndirectCommand(s_GeneralGaussianSplatFoveatedMidIndirect.command,
                         s_GeneralGaussianSplatFoveatedMidVisibleCount.visibleCount);
    writeIndirectCommand(s_GeneralGaussianSplatFoveatedOuterIndirect.command,
                         s_GeneralGaussianSplatFoveatedOuterVisibleCount.visibleCount);
#else
    writeIndirectCommand(s_GeneralGaussianSplatIndirect.command, s_GeneralGaussianSplatVisibleCount.visibleCount);
#endif
}
