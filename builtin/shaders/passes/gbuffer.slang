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

[shader("fragment")] GBufferBase fragmentBase(SurfaceVertex input, bool frontFace : SV_IsFrontFace)
{
    float3 baseColor = readBaseColor(input.uv, input.color).rgb;
    SurfaceData surface = sampleSurface(input, frontFace, baseColor);
    GBufferBase output;
    output.positionMetallic = float4(surface.worldPosition, surface.metallic);
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
