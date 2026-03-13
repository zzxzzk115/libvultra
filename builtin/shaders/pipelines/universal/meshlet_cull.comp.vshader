[vshader]
language = glsl
version = 460

[comp]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_INSTANCE_BUFFER
#define VULTRA_DECLARE_MESH_TABLE_BUFFER
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MODEL_BUFFER
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
    uint instanceIndex = gl_GlobalInvocationID.x;
    if (instanceIndex >= u_PC.instanceCount)
        return;

    GpuInstance inst = s_Instances.instances[instanceIndex];
    GpuMeshEntry mesh = s_MeshTable.meshes[inst.meshIndex];
    mat4 model = s_Models.models[inst.transformIndex];

    vec3 cameraPosWS = u_Camera.inverseView[3].xyz;
    float maxScale = extract_max_scale(model);

    for (uint i = 0u; i < mesh.meshletCount; ++i)
    {
        uint meshletIndex = mesh.meshletOffset + i;
        Meshlet m = s_Meshlets.meshlets[meshletIndex];

        vec3 centerWS = (model * vec4(m.center, 1.0)).xyz;
        float radiusWS = m.radius * maxScale;

        // 1. Frustum test
        // if (!sphere_frustum_test(u_Camera, centerWS, radiusWS))
        //     continue;

        // 2. Cone culling
        if (u_PC.enableConeCull != 0u)
        {
            // transform direction with w=0, then normalize.
            vec3 coneAxisWS = normalize((model * vec4(m.coneAxis, 0.0)).xyz);
            // Usually coneCutoff == 1 means invalid / disabled cone.
            if (m.coneCutoff < 1.0)
            {
                if (!cone_visible_alanwake2(coneAxisWS, m.coneCutoff, centerWS, radiusWS, cameraPosWS))
                    continue;
            }
        }

        uint outIndex = atomicAdd(s_VisibleCount.visibleCount, 1u);
        if (outIndex < u_PC.maxVisibleMeshlets)
        {
            s_VisibleMeshlets.visibleMeshlets[outIndex].meshletIndex = meshletIndex;
            s_VisibleMeshlets.visibleMeshlets[outIndex].instanceIndex = instanceIndex;
            s_VisibleMeshlets.visibleMeshlets[outIndex].materialIndex = m.materialIndex;
            s_VisibleMeshlets.visibleMeshlets[outIndex].flags = 0u;
        }
    }
}