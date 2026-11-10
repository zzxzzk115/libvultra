#include "lib/surface_data.slangh"
#include "lib/surface_lighting.slangh"

#if defined(VULTRA_TRANSPARENCY_SAMPLED_DEPTH) && defined(VULTRA_TRANSPARENCY_EXPLICIT_DEPTH)
#error Sampled and explicit fragment depth are mutually exclusive
#endif
#if defined(VULTRA_TRANSPARENCY_EARLY_DEPTH) && (defined(VULTRA_TRANSPARENCY_SAMPLED_DEPTH) || defined(VULTRA_TRANSPARENCY_EXPLICIT_DEPTH))
#error Early depth tests require the raster depth attachment
#endif

[shader("vertex")]
SurfaceVertex vertexMain(MeshVertex input)
{
    return transformSurfaceVertex(input);
}

[shader("fragment")]
#ifdef VULTRA_TRANSPARENCY_EARLY_DEPTH
[earlydepthstencil]
#endif
#ifdef VULTRA_TRANSPARENCY_EXPLICIT_DEPTH
float4 fragmentMain(SurfaceVertex input, bool frontFace : SV_IsFrontFace, out float fragmentDepth : SV_Depth) : SV_Target
#else
float4 fragmentMain(SurfaceVertex input, bool frontFace : SV_IsFrontFace) : SV_Target
#endif
{
#ifdef VULTRA_TRANSPARENCY_EXPLICIT_DEPTH
    fragmentDepth = input.position.z;
#endif
#if defined(VULTRA_TRANSPARENCY_SAMPLED_DEPTH) || defined(VULTRA_TRANSPARENCY_EARLY_DEPTH)
    // Capture quad gradients before the sampled depth or material alpha tests.
    float2 dx = ddx(input.uv);
    float2 dy = ddy(input.uv);
#else
    float2 dx = 0;
    float2 dy = 0;
#endif
#ifdef VULTRA_TRANSPARENCY_SAMPLED_DEPTH
    if (input.position.z > transparentOpaqueDepth.Load(int3(int2(input.position.xy), 0)))
    {
        discard;
    }
#endif
    float4 baseColor = readBaseColor(input.uv, input.color, dx, dy);
    SurfaceData surface = sampleSurface(input, frontFace, baseColor.rgb, dx, dy);
    return float4(shadeSurface(surface), saturate(baseColor.a));
}
