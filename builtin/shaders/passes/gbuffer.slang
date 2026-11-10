#include "lib/surface_data.slangh"

[shader("vertex")] SurfaceVertex vertexMain(MeshVertex input)
{
    return transformSurfaceVertex(input);
}

struct GBufferBase
{
    float4 positionMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 albedoWeight : SV_Target2;
    float4 emissionOcclusion : SV_Target3;
};

float4 encodeGBufferPosition(SurfaceVertex input, float metallic)
{
#if defined(VULTRA_GBUFFER_DEPTH_32)
    return float4(input.position.z, metallic, 0, 0);
#elif defined(VULTRA_GBUFFER_CAMERA_RELATIVE_16)
    return float4(input.worldPosition - frame.cameraPosition.xyz, metallic);
#else
    return float4(input.worldPosition, metallic);
#endif
}

[shader("fragment")] GBufferBase fragmentBase(SurfaceVertex input, bool frontFace : SV_IsFrontFace)
{
    float3 baseColor = readBaseColor(input.uv, input.color).rgb;
    SurfaceData surface = sampleSurface(input, frontFace, baseColor);
    GBufferBase output;
    output.positionMetallic = encodeGBufferPosition(input, surface.metallic);
    output.normalRoughness = float4(surface.normal, surface.roughness);
    output.albedoWeight = float4(surface.baseColor, surface.baseWeight);
    output.emissionOcclusion = float4(surface.emission, surface.occlusion);
    return output;
}

struct GBufferMaterial
{
    float4 specular : SV_Target0;
    float4 geometricNormalIor : SV_Target1;
    float4 coat : SV_Target2;
};

[shader("fragment")] GBufferMaterial fragmentMaterial(SurfaceVertex input, bool frontFace : SV_IsFrontFace)
{
    float3 baseColor = readBaseColor(input.uv, input.color).rgb;
    SurfaceData surface = sampleSurface(input, frontFace, baseColor);
    GBufferMaterial output;
    output.specular = float4(surface.specularColor, surface.specularWeight);
    output.geometricNormalIor = float4(surface.geometricNormal, surface.specularIor);
    output.coat = float4(surface.coatWeight, surface.coatRoughness,
                         surface.coatIor, surface.baseDiffuseRoughness);
    return output;
}

// A single rasterization of the same material for devices that support seven MRTs.
struct GBufferCombined
{
    float4 positionMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 albedoWeight : SV_Target2;
    float4 emissionOcclusion : SV_Target3;
    float4 specular : SV_Target4;
    float4 geometricNormalIor : SV_Target5;
    float4 coat : SV_Target6;
};

[shader("fragment")] GBufferCombined fragmentCombined(SurfaceVertex input, bool frontFace : SV_IsFrontFace)
{
    float3 baseColor = readBaseColor(input.uv, input.color).rgb;
    SurfaceData surface = sampleSurface(input, frontFace, baseColor);
    GBufferCombined output;
    output.positionMetallic = encodeGBufferPosition(input, surface.metallic);
    output.normalRoughness = float4(surface.normal, surface.roughness);
    output.albedoWeight = float4(surface.baseColor, surface.baseWeight);
    output.emissionOcclusion = float4(surface.emission, surface.occlusion);
    output.specular = float4(surface.specularColor, surface.specularWeight);
    output.geometricNormalIor = float4(surface.geometricNormal, surface.specularIor);
    output.coat = float4(surface.coatWeight, surface.coatRoughness,
                         surface.coatIor, surface.baseDiffuseRoughness);
    return output;
}
