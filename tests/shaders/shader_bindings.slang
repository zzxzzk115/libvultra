#ifndef INPUT_COUNT
#define INPUT_COUNT 2
#endif

struct Parameters
{
#ifdef FLOAT_MULTIPLIER
    float multiplier;
#else
    uint multiplier;
#endif
    int bias;
    bool enabled;
    float gain;
};
struct Push { uint increment; };

[[vk::binding(0, 0)]] StructuredBuffer<uint> single[1];
[[vk::binding(1, 0)]] StructuredBuffer<uint> inputs[INPUT_COUNT];
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> result;
[[vk::binding(5, 3)]] ConstantBuffer<Parameters> parameters;
[[vk::push_constant]] ConstantBuffer<Push> push;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint sum = single[0][0];
    for (uint i = 0; i < INPUT_COUNT; ++i)
    {
        sum += inputs[i][0];
    }
    result[0] = parameters.enabled ? uint((int(sum * parameters.multiplier) + parameters.bias +
                                         int(push.increment)) * parameters.gain) : 0;
#ifdef OUTPUT_OFFSET
    result[0] += OUTPUT_OFFSET;
#endif
}
