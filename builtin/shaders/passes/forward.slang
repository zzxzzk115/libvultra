#include "lib/surface_lighting.slangh"

[shader("vertex")] SurfaceVertex vertexMain(MeshVertex input)
{
    return transformSurfaceVertex(input);
}

[shader("fragment")] float4 fragmentMain(SurfaceVertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    float3 baseColor = readBaseColor(input.uv, input.color).rgb;
    if (material.meshlets.w != 0)
    {
        return float4(input.color.rgb, 1);
    }
    SurfaceData surface = sampleSurface(input, frontFace, baseColor);
    return float4(shadeSurface(surface), 1);
}
