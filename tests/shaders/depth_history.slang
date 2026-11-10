[[vk::binding(0, 0)]] Texture2DMSArray<float> depthImage;
[[vk::binding(1, 0)]] [[vk::image_format("unknown")]] Texture2DMSArray<uint> stencilImage;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint2> result;

struct Parameters
{
    uint width;
    uint height;
    uint layers;
    uint samples;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

[shader("compute")][numthreads(8, 8, 1)]
void sampleDepthStencilMs(uint3 pixel : SV_DispatchThreadID)
{
    if (pixel.x >= parameters.width || pixel.y >= parameters.height ||
        pixel.z >= parameters.layers * parameters.samples)
    {
        return;
    }
    const uint layer = pixel.z / parameters.samples;
    const uint sample = pixel.z % parameters.samples;
    const int3 location = int3(pixel.xy, layer);
    const uint index = (pixel.z * parameters.height + pixel.y) * parameters.width + pixel.x;
    result[index] = uint2(asuint(depthImage.Load(location, sample)), stencilImage.Load(location, sample));
}
