[vshader]
id       = "builtin/highend/coarse_instance_cull.comp"
language = glsl
version = 460

[comp]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_INSTANCE_BUFFER
#define VULTRA_DECLARE_MESH_TABLE_BUFFER
#define VULTRA_DECLARE_MODEL_BUFFER
#define VULTRA_DECLARE_VISIBLE_INSTANCE_BUFFER
#define VULTRA_DECLARE_VISIBLE_INSTANCE_COUNT_BUFFER
#define VULTRA_DECLARE_DISPATCH_ARGS_BUFFER
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform CoarseCullPushConstants
{
    uint instanceCount;
    uint maxVisibleInstances;
    uint padding0;
    uint padding1;
} u_PC;

void main()
{
    uint instanceIndex = gl_GlobalInvocationID.x;
    if (instanceIndex >= u_PC.instanceCount)
        return;

    GpuInstance inst = s_Instances.instances[instanceIndex];
    GpuMeshEntry mesh = s_MeshTable.meshes[inst.meshIndex];
    if (mesh.meshletCount == 0u)
        return;

    mat4 model = s_Models.models[inst.transformIndex];
    float maxScale = extract_max_scale(model);

    vec3 centerWS = (model * vec4(mesh.boundsCenter, 1.0)).xyz;
    float radiusWS = mesh.boundsRadius * maxScale;

    if (!sphere_frustum_test(centerWS, radiusWS))
        return;

    uint outIndex = atomicAdd(s_VisibleInstanceCount.visibleInstanceCount, 1u);
    if (outIndex >= u_PC.maxVisibleInstances)
    {
        // Keep the published count bounded to valid storage range.
        atomicMin(s_VisibleInstanceCount.visibleInstanceCount, u_PC.maxVisibleInstances);
        return;
    }

    s_VisibleInstances.instanceIndices[outIndex] = instanceIndex;
    uint requiredGroups = (outIndex >> 6u) + 1u;
    atomicMax(s_DispatchArgs.groupCountX, requiredGroups);
}
