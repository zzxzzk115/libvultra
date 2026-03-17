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
#define VULTRA_DECLARE_HZB_TEXTURE
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform HiZCullPushConstants
{
    uint drawCount;
    uint hzbMipCount;
    uint enableHiZ;
    uint instanceCount;
    uint transformCount;
    uint meshletCount;
    uint padding0;
} u_PC;

void main()
{
    const uint meshletSlot = gl_GlobalInvocationID.x;
    if (meshletSlot >= u_PC.drawCount)
        return;

    if (meshletSlot >= s_VisibleCount.visibleCount)
        return;

    if (u_PC.enableHiZ == 0u)
        return;

    const uint kMeshletVisibleFlag = 1u;
    GpuVisibleMeshlet vis = s_VisibleMeshlets.visibleMeshlets[meshletSlot];
    if ((vis.flags & kMeshletVisibleFlag) == 0u)
        return;

    if (vis.instanceIndex >= u_PC.instanceCount)
        return;
    if (vis.meshletIndex >= u_PC.meshletCount)
        return;

    GpuInstance inst = s_Instances.instances[vis.instanceIndex];
    if (inst.transformIndex >= u_PC.transformCount)
        return;

    Meshlet m        = s_Meshlets.meshlets[vis.meshletIndex];
    mat4 model       = s_Models.models[inst.transformIndex];

    vec3 centerWS = (model * vec4(m.center, 1.0)).xyz;
    vec4 clip     = u_Camera.viewProjection * vec4(centerWS, 1.0);
    if (clip.w <= 1e-6)
        return;

    vec3 ndc = clip.xyz / clip.w;
    if (any(isnan(ndc)) || any(isinf(ndc)))
        return;
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
        return;

    if (u_PC.hzbMipCount == 0u)
        return;

    ivec2 size0  = textureSize(u_HzbTexture, 0);
    if (size0.x <= 0 || size0.y <= 0)
        return;

    vec2 uv      = ndc.xy * 0.5 + 0.5;
    ivec2 coord0 = clamp(ivec2(uv * vec2(size0)), ivec2(0), size0 - ivec2(1));

    float hzbDepth  = texelFetch(u_HzbTexture, coord0, 0).r;
    if (isnan(hzbDepth) || isinf(hzbDepth))
        return;

    float meshDepth = ndc.z;

    const float kDepthBias = 0.002;
    if (meshDepth > (hzbDepth + kDepthBias))
    {
        vis.flags &= ~kMeshletVisibleFlag;
        s_VisibleMeshlets.visibleMeshlets[meshletSlot] = vis;
    }
}
