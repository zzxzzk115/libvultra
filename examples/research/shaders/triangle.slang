#include "color.slangh"

struct Parameters
{
    float4x4 transform;
    float4   tint;
    uint     decodeSrgb;
    uint     padding0;
    uint     padding1;
    uint     padding2;
};

[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

struct Vertex
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")] Vertex vertexMain(uint id
                                     : SV_VertexID)
{
    const float3 positions[3] = {float3(-0.65, -0.55, 0.5), float3(0, 0.65, 0.5), float3(0.65, -0.55, 0.5)};
    const float3 colors[3]    = {float3(1, 0.2, 0.12), float3(0.2, 1, 0.45), float3(0.18, 0.4, 1)};

    Vertex output;
    output.position = mul(parameters.transform, float4(positions[id], 1));
    output.color    = colors[id];

    return output;
}

    [shader("fragment")] float4 fragmentMain(Vertex input) :
    SV_Target
{
    float3 color = experimentColor(input.color) * parameters.tint.rgb;
    if (parameters.decodeSrgb != 0)
    {
        color = select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
    }
    return float4(color, 1);
}
