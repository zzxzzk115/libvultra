struct Parameters
{
    uint multiplier;
    int bias;
    bool enabled;
    float gain;
};
struct Adjustments { uint offset; };
struct Frame { uint base; };
struct Stage { ConstantBuffer<Parameters> parameters; };
[[vk::binding(0, 0)]] Stage stage;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> result;
[[vk::binding(3, 1)]] ConstantBuffer<Adjustments> adjustments[1];
[[vk::binding(0, 2)]] ConstantBuffer<Frame> frame;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    // Zero-initialized frame data must still keep the float-to-uint conversion in range.
    result[0] = stage.parameters.enabled ?
        uint(max(0.0, (int(frame.base * stage.parameters.multiplier) + stage.parameters.bias) * stage.parameters.gain)) +
        adjustments[0].offset : 0;
}
