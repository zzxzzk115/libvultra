[vshader]
language = glsl
version = 460

[comp]
#define VULTRA_DECLARE_DEPTH_TEXTURE
#define VULTRA_DECLARE_HZB_STORAGE
#include "include/common/gpu_scene.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant) uniform HzbPushConstants
{
    uint srcWidth;
    uint srcHeight;
    uint mipCount;
    uint padding0;
} u_PC;

void main()
{
    if (gl_GlobalInvocationID.x >= u_PC.srcWidth || gl_GlobalInvocationID.y >= u_PC.srcHeight)
        return;

    // Stage-1 implementation: seed HZB mip0 from depth.
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    float depthValue = texelFetch(u_DepthTexture, coord, 0).r;
    imageStore(u_HzbStorage, coord, vec4(depthValue, 0.0, 0.0, 0.0));
}
