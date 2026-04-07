[vshader]
language = glsl
version = 460

[comp]
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

struct DrawIndirectCommand
{
    uint count;
    uint instanceCount;
    uint first;
    uint firstInstance;
};

layout(set = 0, binding = 12, std430) buffer IndirectBuffer
{
    DrawIndirectCommand commands[];
} s_Indirect;

layout(set = 0, binding = 20, std430) readonly buffer VisibleCount
{
    uint count;
} s_VisibleCount;

void main()
{
    DrawIndirectCommand cmd;
    cmd.count = 4u;
    cmd.instanceCount = s_VisibleCount.count;
    cmd.first = 0u;
    cmd.firstInstance = 0u;
    s_Indirect.commands[0] = cmd;
}
