#if !defined(VULTRA_OPENPBR_REFERENCE) && !defined(VULTRA_OPENPBR_OPAQUE_SURFACE)
// The current deferred surface has no transmission, subsurface or dispersion inputs.
#define VULTRA_OPENPBR_OPAQUE_SURFACE 1
#endif
#include "lib/surface_lighting.slangh"
#include "lib/emission_surface.slangh"
#include "resources/fullscreen_vertex.slangh"
#include "resources/gbuffer.slangh"

#ifdef VULTRA_CLUSTERED_LIGHTING
[[vk::binding(20, 0)]] StructuredBuffer<uint2> lightClusters;
#endif

[shader("fragment")] float4 fragmentMain(ScreenVertex input) : SV_Target
{
    int2 pixel = int2(input.position.xy);
    float4 positionMetallic = gbufferPositionMetallic.Load(int3(pixel, 0));
#ifdef VULTRA_GBUFFER_DEPTH_32
    if (positionMetallic.y < 0)
#else
    if (positionMetallic.w < 0)
#endif
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
#if defined(VULTRA_GBUFFER_DEPTH_32)
    uint positionWidth, positionHeight;
    gbufferPositionMetallic.GetDimensions(positionWidth, positionHeight);
    surface.worldPosition = reconstructGBufferPosition(pixel, positionMetallic.x,
                                                       uint2(positionWidth, positionHeight),
                                                       frame.inverseViewProjection);
    surface.metallic = positionMetallic.y;
#else
    surface.worldPosition = positionMetallic.xyz;
#if defined(VULTRA_GBUFFER_CAMERA_RELATIVE_16)
    surface.worldPosition += frame.cameraPosition.xyz;
#endif
    surface.metallic = positionMetallic.w;
#endif
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
    // Unlit surfaces share the existing attachments without invoking lighting or sampling the environment.
    if (isEmissionOnlySurface(surface))
    {
        return float4(shadeEmissionOnlySurface(surface), 1);
    }
#ifdef VULTRA_CLUSTERED_LIGHTING
    uint width, height;
    gbufferPositionMetallic.GetDimensions(width, height);
    float depth = -mul(frame.view, float4(surface.worldPosition, 1)).z;
    float nearPlane = frame.cameraClip.x;
    float farPlane = frame.cameraClip.y;
    uint2 mask = uint2(0xffffffffu, 0xffffffffu);
    if (nearPlane > 0 && farPlane > nearPlane && depth >= nearPlane && depth <= farPlane)
    {
        uint columns = (width + 15) / 16;
        uint rows = (height + 15) / 16;
        uint2 tile = min(uint2(pixel) / 16, uint2(columns - 1, rows - 1));
        uint slice = min(uint(max(log(depth / nearPlane) / log(farPlane / nearPlane), 0) * 24), 23u);
        mask = lightClusters[(slice * rows + tile.y) * columns + tile.x];
    }
    return float4(shadeSurface(surface, frame.cameraPosition.xyz, mask), 1);
#else
    return float4(shadeSurface(surface), 1);
#endif
}
