#include "lib/color.slangh"
#include "resources/fullscreen_vertex.slangh"

[[vk::binding(0, 0)]] Texture2D<float4> source;
[[vk::binding(1, 0)]] SamplerState      sourceSampler;

struct BlitParameters
{
    uint encodeSrgb;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::push_constant]] ConstantBuffer<BlitParameters> parameters;

[shader("fragment")] float4 blitMain(ScreenVertex input) :
    SV_Target
{
    float4 color = source.SampleLevel(sourceSampler, input.uv, 0);
    if (parameters.encodeSrgb != 0)
    {
        color.rgb = linearToSrgb(max(color.rgb, 0));
    }
    return color;
}
