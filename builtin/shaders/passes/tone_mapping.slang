#include "lib/color.slangh"
[[vk::binding(0, 0)]] Texture2D<float4> hdrColor;
struct ToneParameters
{
    float4 values;
};
[[vk::push_constant]] ConstantBuffer<ToneParameters> toneParameters; // Exposure EV, sRGB transfer, palette bypass, padding.
#include "resources/fullscreen_vertex.slangh"

[shader("fragment")] float4 fragmentMain(ScreenVertex input) :
    SV_Target
{
    float3 color = hdrColor.Load(int3(int2(input.position.xy), 0)).rgb;
    if (toneParameters.values.z < 0.5)
    {
        color = toneMapAces(color * exp2(toneParameters.values.x));
    }
    // Meshlet colors are a display palette; exposure and tone mapping must not alter it.
    // Float output stays linear for XR. Desktop UNORM output receives one display transfer.
    return float4(toneParameters.values.y > 0.5 ? linearToSrgb(color) : color, 1);
}
