#include "lib/space.slangh"

struct Parameters
{
    float2 extent;
    float2 translation;
    uint   textured;
    uint   padding0;
    uint   padding1;
    uint   padding2;
};

[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

[[vk::binding(0, 0)]] Texture2D<float4> uiTexture;
[[vk::binding(1, 0)]] SamplerState uiSampler;

struct VertexInput
{
    float2 position : POSITION;
    float4 color    : COLOR0;
    float2 uv       : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 uv       : TEXCOORD0;
};

[shader("vertex")] VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float2 pixel = input.position + parameters.translation;
    output.position = float4(uvToClip(pixel / parameters.extent), 0, 1);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")] float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 texel = parameters.textured != 0 ? uiTexture.Sample(uiSampler, input.uv) : float4(1, 1, 1, 1);
    return input.color * texel;
}
