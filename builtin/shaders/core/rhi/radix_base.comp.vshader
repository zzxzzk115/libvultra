[vshader]
language = glsl
version = 460

[comp]
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) readonly buffer _BucketTotals { uint bucketTotals[]; };
layout(set = 0, binding = 1, std430) writeonly buffer _BucketBase { uint bucketBase[]; };

shared uint s[256];

void main()
{
    uint t = gl_LocalInvocationID.x;
    uint v = bucketTotals[t];
    s[t] = v;
    barrier();

    for (uint offset = 1u; offset < 256u; offset <<= 1u)
    {
        uint addv = 0u;
        if (t >= offset)
            addv = s[t - offset];
        barrier();
        s[t] = s[t] + addv;
        barrier();
    }

    bucketBase[t] = s[t] - v;
}
