[vshader]
language = glsl
version = 460

[comp]
layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) readonly buffer _BlockHisto { uint blockHisto[]; };
layout(set = 0, binding = 1, std430) writeonly buffer _BlockPrefix { uint blockPrefix[]; };
layout(set = 0, binding = 2, std430) writeonly buffer _BucketTotals { uint bucketTotals[]; };

layout(push_constant) uniform _PC {
    uint shift;
    uint blockCount;
} u_PC;

shared uint s[1024];

void main()
{
    uint bucket = gl_WorkGroupID.x;
    uint t = gl_LocalInvocationID.x;

    uint v = 0u;
    if (t < u_PC.blockCount)
        v = blockHisto[t * 256u + bucket];

    s[t] = v;
    barrier();

    for (uint offset = 1u; offset < 1024u; offset <<= 1u)
    {
        uint addv = 0u;
        if (t >= offset)
            addv = s[t - offset];
        barrier();
        s[t] = s[t] + addv;
        barrier();
    }

    if (t < u_PC.blockCount)
    {
        uint inclusive = s[t];
        blockPrefix[t * 256u + bucket] = inclusive - v;
    }

    if (t == (u_PC.blockCount - 1u))
        bucketTotals[bucket] = s[t];
}
