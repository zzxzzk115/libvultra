#include "lib/surface_data.slangh"
#include "lib/surface_lighting.slangh"

[shader("vertex")] SurfaceVertex vertexMain(MeshVertex input)
{
    return transformSurfaceVertex(input);
}

[shader("fragment")] [earlydepthstencil]
float4 fragmentMain(SurfaceVertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    float3 baseColor = (baseColorTexture.Sample(materialSamplers[0], input.uv) * material.baseColor * input.color).rgb;
    if (material.meshlets.w != 0)
    {
        return float4(input.color.rgb, 1);
    }
    SurfaceData surface = sampleSurface(input, frontFace, baseColor);
    return float4(shadeSurface(surface), 1);
}
