[vshader]
language = glsl
version = 460

[comp]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_INSTANCE_BUFFER
#define VULTRA_DECLARE_MESH_TABLE_BUFFER
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MODEL_BUFFER
#define VULTRA_DECLARE_VISIBLE_INSTANCE_BUFFER
#define VULTRA_DECLARE_VISIBLE_INSTANCE_COUNT_BUFFER
#define VULTRA_DECLARE_VISIBLE_MESHLET_BUFFER
#define VULTRA_DECLARE_VISIBLE_COUNT_BUFFER
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform CullPushConstants
{
    uint instanceCount;
    uint maxVisibleMeshlets;
    uint enableConeCull;
    uint padding0;
} u_PC;

void main()
{
    const uint kMeshletVisibleFlag = 1u;

    uint visibleSlot = gl_GlobalInvocationID.x;
    if (visibleSlot >= u_PC.instanceCount)
        return;

    uint visibleCount = s_VisibleInstanceCount.visibleInstanceCount;
    if (visibleSlot >= visibleCount)
        return;

    uint instanceIndex = s_VisibleInstances.instanceIndices[visibleSlot];

    GpuInstance inst = s_Instances.instances[instanceIndex];
    GpuMeshEntry mesh = s_MeshTable.meshes[inst.meshIndex];
    if (mesh.meshletCount == 0u)
        return;

    mat4 model = s_Models.models[inst.transformIndex];

    float maxScale = extract_max_scale(model);

    // Aggressive coarse cull: if whole mesh bound is off-frustum, skip all meshlets.
    vec3 meshCenterWS = (model * vec4(mesh.boundsCenter, 1.0)).xyz;
    float meshRadiusWS = mesh.boundsRadius * maxScale;
    if (!sphere_frustum_test(meshCenterWS, meshRadiusWS))
        return;

    vec3 cameraPosWS = vec3(0.0);
    if (u_PC.enableConeCull != 0u)
        cameraPosWS = u_Camera.inverseView[3].xyz;

    uint  baseOut = atomicAdd(s_VisibleCount.visibleCount, mesh.meshletCount);
    if (baseOut >= u_PC.maxVisibleMeshlets)
    {
        // Keep the published count bounded to valid storage range.
        atomicMin(s_VisibleCount.visibleCount, u_PC.maxVisibleMeshlets);
        return;
    }

    for (uint i = 0u; i < mesh.meshletCount; ++i)
    {
        uint meshletIndex = mesh.meshletOffset + i;
        Meshlet m = s_Meshlets.meshlets[meshletIndex];

        vec3 centerWS = (model * vec4(m.center, 1.0)).xyz;
        float radiusWS = m.radius * maxScale;

        bool isVisible = true;

        // 1. Frustum test
        if (!sphere_frustum_test(centerWS, radiusWS))
            isVisible = false;

        // 2. Cone culling
        if (isVisible && u_PC.enableConeCull != 0u)
        {
            // transform direction with w=0, then normalize.
            vec3 coneAxisWS = normalize((model * vec4(m.coneAxis, 0.0)).xyz);
            // Usually coneCutoff == 1 means invalid / disabled cone.
            if (m.coneCutoff < 1.0)
            {
                if (!cone_visible_alanwake2(coneAxisWS, m.coneCutoff, centerWS, radiusWS, cameraPosWS))
                    isVisible = false;
            }
        }

        uint outIndex = baseOut + i;
        if (outIndex >= u_PC.maxVisibleMeshlets)
            break;

        s_VisibleMeshlets.visibleMeshlets[outIndex].meshletIndex = meshletIndex;
        s_VisibleMeshlets.visibleMeshlets[outIndex].instanceIndex = instanceIndex;
        s_VisibleMeshlets.visibleMeshlets[outIndex].materialIndex = m.materialIndex;
        s_VisibleMeshlets.visibleMeshlets[outIndex].flags = isVisible ? kMeshletVisibleFlag : 0u;
    }
}