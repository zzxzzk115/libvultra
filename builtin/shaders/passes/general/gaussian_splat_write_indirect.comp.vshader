[vshader]
language = glsl
version = 460

[comp]
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_VISIBLE_COUNT_BUFFER
#define VULTRA_DECLARE_GENERAL_GAUSSIAN_SPLAT_INDIRECT_BUFFER
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{
    s_GeneralGaussianSplatIndirect.command.count         = 4u;
    s_GeneralGaussianSplatIndirect.command.instanceCount = s_GeneralGaussianSplatVisibleCount.visibleCount;
    s_GeneralGaussianSplatIndirect.command.first         = 0u;
    s_GeneralGaussianSplatIndirect.command.firstInstance = 0u;
}
