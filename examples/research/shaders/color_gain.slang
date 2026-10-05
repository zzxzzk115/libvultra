[[vk::binding(0, 0)]] Texture2D<float4> source;
[[vk::binding(1, 0)]] RWTexture2D<float4> destination;

struct GainParameters
{
    float gain;
    // Scalar padding matches the CPU's 16-byte push-constant block without vector alignment gaps.
    float padding0;
    float padding1;
    float padding2;
};

[[vk::push_constant]] ConstantBuffer<GainParameters> parameters;

[shader("compute")]
[numthreads(8, 8, 1)]
void gainMain(uint3 thread : SV_DispatchThreadID)
{
    uint width;
    uint height;
    destination.GetDimensions(width, height);
    if (thread.x >= width || thread.y >= height)
    {
        return;
    }
    // Integer texel loads preserve VRI's image coordinates; both resources contain linear HDR color.
    float4 color = source.Load(int3(thread.xy, 0));
    destination[thread.xy] = float4(color.rgb * parameters.gain, color.a);
}
