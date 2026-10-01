#include "lib/surface_lighting.slangh"
#include "resources/fullscreen_vertex.slangh"
#include "resources/gbuffer.slangh"

[shader("fragment")] float4 fragmentMain(ScreenVertex input) : SV_Target
{
    int2 pixel = int2(input.position.xy);
    float4 positionMetallic = gbufferPositionMetallic.Load(int3(pixel, 0));
    if (positionMetallic.w < 0)
    {
        discard; // Preserve the skybox where geometry did not write the G-buffer.
    }
    float4 normalRoughness = gbufferNormalRoughness.Load(int3(pixel, 0));
    float4 albedoWeight = gbufferAlbedoWeight.Load(int3(pixel, 0));
    float4 emissionOcclusion = gbufferEmissionOcclusion.Load(int3(pixel, 0));
    float4 specular = gbufferSpecular.Load(int3(pixel, 0));
    float4 geometricNormalIor = gbufferGeometricNormalIor.Load(int3(pixel, 0));
    float4 coat = gbufferCoat.Load(int3(pixel, 0));
    SurfaceData surface;
    surface.worldPosition = positionMetallic.xyz;
    surface.metallic = positionMetallic.w;
    surface.normal = normalize(normalRoughness.xyz);
    surface.roughness = normalRoughness.w;
    surface.baseColor = albedoWeight.xyz;
    surface.baseWeight = albedoWeight.w;
    surface.emission = emissionOcclusion.xyz;
    surface.occlusion = emissionOcclusion.w;
    surface.specularColor = specular.xyz;
    surface.specularWeight = specular.w;
    surface.geometricNormal = normalize(geometricNormalIor.xyz);
    surface.specularIor = geometricNormalIor.w;
    surface.coatWeight = coat.x;
    surface.coatRoughness = coat.y;
    surface.coatIor = coat.z;
    surface.baseDiffuseRoughness = coat.w;
    return float4(shadeSurface(surface), 1);
}
