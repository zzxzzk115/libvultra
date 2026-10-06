#include "lib/color.slangh"
#include "resources/fullscreen_vertex.slangh"

[[vk::binding(0, 0)]] Texture2D<float4> source;
[[vk::binding(1, 0)]] SamplerState      sourceSampler;

struct BlitParameters
{
    uint encodeSrgb;
    uint channel;
    float minimum;
    float maximum;
    uint remap;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::push_constant]] ConstantBuffer<BlitParameters> parameters;

[shader("fragment")] float4 blitMain(ScreenVertex input) :
    SV_Target
{
    float4 color = source.SampleLevel(sourceSampler, input.uv, 0);
    if (parameters.remap != 0)
    {
        float3 selected = color.rgb;
        if (parameters.channel != 0)
        {
            float scalar = parameters.channel == 5 ?
                               dot(color.rgb, float3(0.2126, 0.7152, 0.0722)) :
                               color[parameters.channel - 1];
            selected = float3(scalar);
        }
        color = float4(saturate((selected - parameters.minimum) /
                                (parameters.maximum - parameters.minimum)), 1);
    }
    if (parameters.encodeSrgb != 0)
    {
        color.rgb = linearToSrgb(max(color.rgb, 0));
    }
    return color;
}
