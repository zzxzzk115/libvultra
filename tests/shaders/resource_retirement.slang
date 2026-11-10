#ifndef OUTPUT_BASE
#define OUTPUT_BASE 7
#endif

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> output;
[[vk::binding(1, 0)]] ByteAddressBuffer sceneVertices;

[shader("compute")][numthreads(64, 1, 1)]
void writeValues(uint3 index : SV_DispatchThreadID)
{
    if (index.x < 256)
    {
        output[index.x] = OUTPUT_BASE + index.x;
    }
}

[shader("compute")][numthreads(64, 1, 1)]
void readScene(uint3 index : SV_DispatchThreadID)
{
    if (index.x < 48)
    {
        output[index.x] = sceneVertices.Load(index.x * 4);
    }
}
