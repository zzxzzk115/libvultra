[[vk::binding(0, 0)]] Texture1D<float4> image1D;
[[vk::binding(1, 0)]] Texture1DArray<float4> image1DArray;
[[vk::binding(2, 0)]] Texture2D<float4> image2D;
[[vk::binding(3, 0)]] Texture2DArray<float4> image2DArray;
[[vk::binding(4, 0)]] Texture3D<float4> image3D;
[[vk::binding(5, 0)]] TextureCube<float4> imageCube;
[[vk::binding(6, 0)]] TextureCubeArray<float4> imageCubeArray;
[[vk::binding(7, 0)]] Texture2D<float> imageDepth;
// A sampled combined-format stencil view must not declare a concrete SPIR-V image format.
[[vk::binding(8, 0)]] [[vk::image_format("unknown")]] Texture2D<uint> imageStencil;
[[vk::binding(9, 0)]] Texture2DMS<float4> imageMS;
[[vk::binding(10, 0)]] Texture2DMSArray<float4> imageMSArray;
[[vk::binding(11, 0)]] RWStructuredBuffer<uint4> result;
[[vk::binding(12, 0)]] SamplerState nearestSampler;

struct Parameters
{
    uint layer;
    uint mip;
    uint z;
    uint reserved;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

float3 cubeDirection(uint face)
{
    switch (face)
    {
        case 0: return float3(1, 0, 0);
        case 1: return float3(-1, 0, 0);
        case 2: return float3(0, 1, 0);
        case 3: return float3(0, -1, 0);
        case 4: return float3(0, 0, 1);
        default: return float3(0, 0, -1);
    }
}

[shader("compute")][numthreads(1, 1, 1)]
void sample1D()
{
    uint width, levels;
    image1D.GetDimensions(0, width, levels);
    result[0] = asuint(image1D.Load(int2(0, 0)));
    result[1] = uint4(width, 1, 1, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sample1DArray()
{
    uint width, layers, levels;
    image1DArray.GetDimensions(0, width, layers, levels);
    result[0] = asuint(image1DArray.Load(int3(0, parameters.layer, 0)));
    result[1] = uint4(width, 1, layers, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sample2D()
{
    uint width, height, levels;
    image2D.GetDimensions(parameters.mip, width, height, levels);
    result[0] = asuint(image2D.Load(int3(0, 0, parameters.mip)));
    result[1] = uint4(width, height, 1, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sample2DArray()
{
    uint width, height, layers, levels;
    image2DArray.GetDimensions(parameters.mip, width, height, layers, levels);
    result[0] = asuint(image2DArray.Load(int4(0, 0, parameters.layer, parameters.mip)));
    result[1] = uint4(width, height, layers, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sample3D()
{
    uint width, height, depth, levels;
    image3D.GetDimensions(parameters.mip, width, height, depth, levels);
    result[0] = asuint(image3D.Load(int4(0, 0, parameters.z, parameters.mip)));
    result[1] = uint4(width, height, depth, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sampleCube()
{
    uint width, height, levels;
    imageCube.GetDimensions(parameters.mip, width, height, levels);
    result[0] = asuint(imageCube.SampleLevel(nearestSampler, cubeDirection(parameters.layer), parameters.mip));
    result[1] = uint4(width, height, 6, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sampleCubeArray()
{
    uint width, height, cubes, levels;
    imageCubeArray.GetDimensions(parameters.mip, width, height, cubes, levels);
    float4 direction = float4(cubeDirection(parameters.layer % 6), parameters.layer / 6);
    result[0] = asuint(imageCubeArray.SampleLevel(nearestSampler, direction, parameters.mip));
    result[1] = uint4(width, height, cubes * 6, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sampleDepth()
{
    uint width, height, levels;
    imageDepth.GetDimensions(parameters.mip, width, height, levels);
    result[0] = asuint(float4(imageDepth.Load(int3(0, 0, parameters.mip)), 0, 0, 1));
    result[1] = uint4(width, height, 1, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sampleStencil()
{
    uint width, height, levels;
    imageStencil.GetDimensions(parameters.mip, width, height, levels);
    result[0] = uint4(imageStencil.Load(int3(0, 0, parameters.mip)), 0, 0, 1);
    result[1] = uint4(width, height, 1, levels);
}

[shader("compute")][numthreads(1, 1, 1)]
void sampleMS()
{
    uint width, height, samples;
    imageMS.GetDimensions(width, height, samples);
    result[0] = asuint(imageMS.Load(int2(0), parameters.z));
    result[1] = uint4(width, height, 1, samples);
}

[shader("compute")][numthreads(1, 1, 1)]
void sampleMSArray()
{
    uint width, height, layers, samples;
    imageMSArray.GetDimensions(width, height, layers, samples);
    result[0] = asuint(imageMSArray.Load(int3(0, 0, parameters.layer), parameters.z));
    result[1] = uint4(width, height, layers, samples);
}
