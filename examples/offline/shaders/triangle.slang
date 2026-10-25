#include "color.slangh"
#include "lib/color.slangh"
#include "triangle.slangh"

struct Parameters
{
    float4x4 transform;
    float4   tint;
    uint     encodeSrgb;
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
    Vertex output;
    output.position = mul(parameters.transform, float4(kTrianglePositions[id], 1));
    output.color    = kTriangleColors[id];

    return output;
}

[shader("fragment")] float4 fragmentMain(Vertex input) :
    SV_Target
{
    float3 color = experimentColor(input.color) * parameters.tint.rgb;
    if (parameters.encodeSrgb != 0)
    {
        color = linearToSrgb(max(color, 0));
    }
    return float4(color, 1);
}
