[[vk::binding(0, 0)]] StructuredBuffer<uint> source;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> output;
[[vk::binding(2, 0)]] SamplerState nearestSampler;
[[vk::binding(3, 0)]] Texture2D<float4> directTexture;
[[vk::binding(0, 1)]] Texture2D<float4> textures[2];

struct Parameters
{
    uint destination;
    uint sourceIndex;
    uint textureIndex;
    uint scale;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

uint evaluate()
{
    float arrayValue = textures[parameters.textureIndex].SampleLevel(nearestSampler, float2(0.5), 0).r;
    float directValue = directTexture.SampleLevel(nearestSampler, float2(0.5), 0).r;
    return source[parameters.sourceIndex] * parameters.scale + uint(arrayValue + directValue);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    output[parameters.destination] = evaluate();
}

[shader("compute")]
[numthreads(1, 1, 1)]
void alternateMain()
{
    output[parameters.destination] = evaluate() + 100;
}
