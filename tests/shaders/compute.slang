[[vk::binding(0, 0)]] StructuredBuffer<float>   first;
[[vk::binding(1, 0)]] StructuredBuffer<float>   second;
[[vk::binding(2, 0)]] RWStructuredBuffer<float> result;

struct Parameters
{
    uint  count;
    float gain;
};

[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[shader("compute")][numthreads(64, 1, 1)] void   main(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x < parameters.count)
    {
        result[thread.x] = (first[thread.x] + second[thread.x]) * parameters.gain;
    }
}
