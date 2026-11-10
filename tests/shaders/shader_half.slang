#ifndef NEW_LAYOUT
#define NEW_LAYOUT 0
#endif

struct Controls
{
#if NEW_LAYOUT
    float             tail;
    half4             tint;
    row_major half2x3 basis;
    half              weights[3];
    half              bias;
    half              gain;
#else
    half                 gain;
    half                 bias;
    half4                tint;
    half                 weights[2];
    column_major half2x3 basis;
    float                tail;
#endif
    uint enabled;
};

ParameterBlock<Controls>                                               material;
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
    half2 basis = mul(material.basis, half3(1, 2, 1));
    half  gain  = material.gain + material.weights[0] + material.weights[1];
#if NEW_LAYOUT
    gain += material.weights[2];
#endif
    float3 color = float3(material.tint.rgb) * float(gain) + float(material.bias) + material.tail;
    color += float3(float2(basis), material.enabled != 0 ? 1 : 0);
    color.xy += float2(pixel.xy) / 32.0;
    output[pixel.xy] = float4(color, float(material.tint.a));
}
