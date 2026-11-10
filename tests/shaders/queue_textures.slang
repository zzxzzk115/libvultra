struct Parameters
{
    uint cycle;
    uint iterations;
    uint width;
    uint height;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[[vk::binding(0, 0)]] Texture2D<uint> source;
[[vk::binding(1, 0), vk::image_format("r32ui")]] RWTexture2D<uint> destination;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> scratch;

uint evaluate(uint seed)
{
    for (uint i = 0; i < parameters.iterations; ++i)
    {
        seed = (seed ^ (seed >> 13)) * 1664525u + 1013904223u;
    }
    return seed;
}

[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 position = float2((vertex << 1) & 2, vertex & 2);
    return float4(position * 2 - 1, 0, 1);
}

[shader("fragment")]
uint fragmentMain(float4 position : SV_Position) : SV_Target
{
    uint index = uint(position.y) * parameters.width + uint(position.x);
    return evaluate(0x10203u + parameters.cycle * 4099u + index);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void independentMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= parameters.width || tid.y >= parameters.height)
    {
        return;
    }
    uint index = tid.y * parameters.width + tid.x;
    scratch[index] = evaluate(0x60107u + parameters.cycle * 6151u + index);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void consumeMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= parameters.width || tid.y >= parameters.height)
    {
        return;
    }
    uint index = tid.y * parameters.width + tid.x;
    destination[tid.xy] = source.Load(int3(tid.xy, 0)) ^ scratch[index];
}
