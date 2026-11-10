#include "lib/float16.slangh"

[[vk::binding(0, 0)]] StructuredBuffer<float4> inputValues;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> floatResults;
[[vk::binding(2, 0), vk::image_format("rgba16f")]] RWTexture2D<float4> halfResults;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    uint width;
    uint height;
    halfResults.GetDimensions(width, height);
    if (thread.x >= width || thread.y >= height)
    {
        return;
    }
    uint index = thread.x + width * thread.y;
    float4 value = inputValues[index];
    float4 rounded = float4(roundToHalfEven(value.x), roundToHalfEven(value.y),
                             roundToHalfEven(value.z), roundToHalfEven(value.w));
    floatResults[index] = rounded;
    halfResults[thread.xy] = rounded;
}
