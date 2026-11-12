struct Parameters
{
    float4x4 transform; // Model to clip space.
    float4   light;     // xyz: unit direction to the light in model space.
    float4   color;     // Linear RGB.
    float4   options;   // x: draw one-metre grid lines over the model's XZ plane; y: encode sRGB.
};

[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 local : POSITION0;
};

[shader("vertex")] VertexOutput vertexMain(float3 position : POSITION, float3 normal : NORMAL)
{
    VertexOutput output;
    output.position = mul(parameters.transform, float4(position, 1));
    output.normal   = normal;
    output.local    = position;
    return output;
}

    [shader("fragment")] float4 fragmentMain(VertexOutput input) :
    SV_Target
{
    const float diffuse = 0.3 + 0.7 * saturate(dot(normalize(input.normal), parameters.light.xyz));
    float3      color   = parameters.color.rgb * diffuse;
    if (parameters.options.x > 0)
    {
        const float2 metres = input.local.xz;
        const float2 lines  = abs(frac(metres - 0.5) - 0.5) / fwidth(metres);
        color               = lerp(float3(0.5, 0.5, 0.5), color, saturate(min(lines.x, lines.y)));
    }
    if (parameters.options.y > 0)
    {
        color = pow(color, 1.0 / 2.2);
    }
    return float4(color, 1);
}
