struct Parameters
{
    float4x4 transform;
    float4   tint;
};

[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")] VertexOutput vertexMain(float3 position
                                           : POSITION, float3 color
                                           : COLOR0)
{
    VertexOutput output;
    output.position = mul(parameters.transform, float4(position, 1));
    output.color    = color;
    return output;
}

    [shader("fragment")] float4 fragmentMain(VertexOutput input) :
    SV_Target
{
    return float4(input.color * parameters.tint.rgb, 1);
}
