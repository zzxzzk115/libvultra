[[vk::binding(0, 0)]] RWStructuredBuffer<uint> input;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> output;
[[vk::push_constant]] uint cycle;

[shader("compute")]
[numthreads(64, 1, 1)]
void producerMain(uint3 tid : SV_DispatchThreadID)
{
    input[tid.x] = tid.x + 11 + cycle * 64;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void consumerMain(uint3 tid : SV_DispatchThreadID)
{
    output[tid.x] = input[tid.x] * 3 + 7;
}
