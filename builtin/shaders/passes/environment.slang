#include "lib/hammersley2d.slangh"
#include "lib/importance_sampling.slangh"
#include "lib/pbr.slangh"
#include "lib/texture.slangh"
#include "resources/fullscreen_vertex.slangh"

[[vk::binding(0, 0)]] Texture2D<float4> environmentMap;
[[vk::binding(1, 0)]] SamplerState      environmentSampler;

struct FilterParameters
{
    float  roughness;
    uint   sampleCount;
    float2 padding;
};

[[vk::push_constant]] ConstantBuffer<FilterParameters> filter;

[shader("fragment")] float4 diffuseMain(ScreenVertex input) :
    SV_Target
{
    float3 normal = environmentDirection(input.uv);
    float3 sum    = 0;
    for (uint i = 0; i < filter.sampleCount; ++i)
    {
        float2 xi        = hammersley(i, filter.sampleCount);
        float  r         = sqrt(xi.x);
        float3 direction = toWorld(float3(r * cos(2 * pi * xi.y), r * sin(2 * pi * xi.y), sqrt(1 - xi.x)), normal);
        sum += environmentMap.SampleLevel(environmentSampler, environmentUv(direction), 0).rgb;
    }
    // Cosine-weighted estimator stores irradiance / pi for the diffuse BRDF.
    return float4(sum / filter.sampleCount, 1);
}

[shader("fragment")] float4 specularMain(ScreenVertex input) :
    SV_Target
{
    float3 normal = environmentDirection(input.uv);
    if (filter.roughness < 0.001)
    {
        return environmentMap.SampleLevel(environmentSampler, input.uv, 0);
    }
    float3 sum    = 0;
    float  weight = 0;
    for (uint i = 0; i < filter.sampleCount; ++i)
    {
        float3 halfVector = sampleGgx(hammersley(i, filter.sampleCount), filter.roughness, normal);
        float3 light      = reflect(-normal, halfVector);
        float  cosine     = saturate(dot(normal, light));
        sum += environmentMap.SampleLevel(environmentSampler, environmentUv(light), 0).rgb * cosine;
        weight += cosine;
    }
    return float4(sum / max(weight, 1e-6), 1);
}

[shader("fragment")] float4 brdfMain(ScreenVertex input) :
    SV_Target
{
    float  ndv       = max(input.uv.x, 0.001);
    float  roughness = max(input.uv.y, 0.02);
    float3 view      = float3(sqrt(1 - ndv * ndv), 0, ndv);
    float2 result    = 0;
    for (uint i = 0; i < filter.sampleCount; ++i)
    {
        float3 halfVector = sampleGgx(hammersley(i, filter.sampleCount), roughness, float3(0, 0, 1));
        float3 light      = reflect(-view, halfVector);
        float  ndl        = saturate(light.z);
        float  ndh        = saturate(halfVector.z);
        float  vdh        = saturate(dot(view, halfVector));
        if (ndl > 0)
        {
            float alpha      = roughness * roughness;
            float visibility = smithG1(ndv, alpha) * smithG1(ndl, alpha) * vdh / max(ndh * ndv, 1e-6);
            float fresnel    = pow(1 - vdh, 5);
            result += float2(1 - fresnel, fresnel) * visibility;
        }
    }
    return float4(result / filter.sampleCount, 0, 1);
}
