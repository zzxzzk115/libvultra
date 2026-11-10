[[vk::binding(0, 0), vk::image_format("unknown")]] Texture2D<float4> imageFloat;
[[vk::binding(1, 0), vk::image_format("unknown")]] Texture2D<uint4> imageUint;
[[vk::binding(2, 0), vk::image_format("unknown")]] Texture2D<int4> imageInt;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint4> output;

[shader("compute")][numthreads(8, 8, 1)]
void readFloatBits(uint3 index : SV_DispatchThreadID)
{
    uint width;
    uint height;
    imageFloat.GetDimensions(width, height);
    if (index.x < width && index.y < height)
    {
        output[index.y * width + index.x] = asuint(imageFloat.Load(int3(index.xy, 0)));
    }
}

[shader("compute")][numthreads(8, 8, 1)]
void readQuantized(uint3 index : SV_DispatchThreadID)
{
    uint width;
    uint height;
    imageFloat.GetDimensions(width, height);
    if (index.x < width && index.y < height)
    {
        output[index.y * width + index.x] = uint4(round(saturate(imageFloat.Load(int3(index.xy, 0))) * 255.0));
    }
}

[shader("compute")][numthreads(8, 8, 1)]
void readUint(uint3 index : SV_DispatchThreadID)
{
    uint width;
    uint height;
    imageUint.GetDimensions(width, height);
    if (index.x < width && index.y < height)
    {
        output[index.y * width + index.x] = imageUint.Load(int3(index.xy, 0));
    }
}

[shader("compute")][numthreads(8, 8, 1)]
void readInt(uint3 index : SV_DispatchThreadID)
{
    uint width;
    uint height;
    imageInt.GetDimensions(width, height);
    if (index.x < width && index.y < height)
    {
        output[index.y * width + index.x] = uint4(imageInt.Load(int3(index.xy, 0)));
    }
}
