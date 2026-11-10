#ifndef NEW_LAYOUT
#define NEW_LAYOUT 0
#endif
struct Layer
{
    float3 tint;
    float  gain;
};

struct Controls
{
    float bias;
};

struct Look
{
#if NEW_LAYOUT
    float              exposure;
    uint               enabled;
    float              extra;
    row_major float2x2 basis;
    Layer              layers[3];
#else
    column_major float2x2 basis;
    Layer                 layers[2];
    uint                  enabled;
    float                 exposure;
#endif
    ConstantBuffer<Controls> controls[NEW_LAYOUT ? 3 : 2];
    Texture2D<float4>        colors[NEW_LAYOUT ? 3 : 2];
    SamplerState             nearest;
};

ParameterBlock<Look> material;

[[vk::binding(0, 0), vk::image_format("rgba32f")]] RWTexture2D<float4> output;

[shader("compute")][numthreads(8, 8, 1)] void main(uint3 pixel : SV_DispatchThreadID)
{
    uint width;
    uint height;
    output.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
    {
        return;
    }
    float2 uv    = (float2(pixel.xy) + 0.5) / float2(width, height);
    float3 a     = material.colors[0].SampleLevel(material.nearest, uv, 0).rgb * material.layers[0].tint;
    float3 b     = material.colors[1].SampleLevel(material.nearest, uv, 0).rgb * material.layers[1].tint;
    float  gain  = material.controls[0].bias + 2 * material.controls[1].bias +
                  material.layers[0].gain + material.layers[1].gain;
#if NEW_LAYOUT
    gain += material.controls[2].bias;
#endif
    float2 basis = mul(material.basis, float2(1, 2));
    float3 color = ((a + b) * gain + float3(basis, material.enabled != 0 ? 1 : 0)) * material.exposure;
#if NEW_LAYOUT
    color += material.extra * material.colors[2].SampleLevel(material.nearest, uv, 0).rgb;
#endif
    // All invocations use the same runtime index; divergent descriptor indexing is a separate device feature.
    output[pixel.xy] = float4(color, material.controls[material.enabled & 1].bias);
}
