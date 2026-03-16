[vshader]
language = glsl
version = 460

[comp]
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) readonly buffer _KeysIn { uint keysIn[]; };
layout(set = 0, binding = 1, std430) readonly buffer _BlockPrefix { uint blockPrefix[]; };
layout(set = 0, binding = 2, std430) readonly buffer _BucketBase { uint bucketBase[]; };
layout(set = 0, binding = 3, std430) writeonly buffer _KeysOut { uint keysOut[]; };
layout(set = 0, binding = 4, std430) readonly buffer _CountBuf { uint count; };

layout(push_constant) uniform _PC {
    uint shift;
    uint blockCount;
} u_PC;

shared uint sOff[256];

void main()
{
    uint tid = gl_LocalInvocationID.x;
    uint block = gl_WorkGroupID.x;
    if (block >= u_PC.blockCount)
        return;

    sOff[tid] = 0u;
    barrier();

    uint base = block * 4096u;
    for (uint j = 0u; j < 16u; ++j)
    {
        uint idx = base + tid + j * 256u;
        if (idx < count)
        {
            uint key = keysIn[idx];
            uint bucket = (key >> u_PC.shift) & 255u;
            uint local = atomicAdd(sOff[bucket], 1u);
            uint global = bucketBase[bucket] + blockPrefix[block * 256u + bucket] + local;
            keysOut[global] = key;
        }
    }
}
