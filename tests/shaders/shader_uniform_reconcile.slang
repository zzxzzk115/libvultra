#ifndef NEW_LAYOUT
#define NEW_LAYOUT 0
#endif

#if NEW_LAYOUT
struct Item
{
    column_major float2x3 basis;
    float2 uv;
};
struct Parameters
{
    float gain;
    bool enabled;
    uint multiplier;
    int bias;
    Item items[3];
    float changed;
    uint fresh;
};
[[vk::binding(3, 1)]] ConstantBuffer<Parameters> parameters;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint4> result;
#else
struct Item
{
    float2 uv;
    row_major float2x3 basis;
};
struct Parameters
{
    uint multiplier;
    int bias;
    bool enabled;
    float gain;
    Item items[2];
    uint obsolete;
    uint changed;
};
[[vk::binding(0, 0)]] ConstantBuffer<Parameters> parameters;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint4> result;
#endif

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint4 value = 0;
    if (parameters.enabled)
    {
        value.x = uint((int(parameters.multiplier * 10) + parameters.bias) * parameters.gain);
        value.y = uint(parameters.items[0].basis[1][2] + parameters.items[1].basis[0][1]);
        value.z = uint(parameters.items[0].uv.x + parameters.items[1].uv.y);
#if NEW_LAYOUT
        value.w = uint(parameters.changed) + parameters.fresh + uint(parameters.items[2].uv.x) +
                  uint(parameters.items[2].basis[1][2]);
#else
        value.w = parameters.obsolete + parameters.changed;
#endif
    }
    result[0] = value;
}
