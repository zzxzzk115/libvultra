#include "lib/space.slangh"
#include "lib/texture.slangh"
#include "resources/frame_block.slangh"
#include "resources/fullscreen_vertex.slangh"

[shader("fragment")] float4 fragmentMain(ScreenVertex input) :
    SV_Target
{
    if (frame.overrides.w > 0.5)
    {
        return float4(0, 0, 0, 1);
    }
    if (frame.options.y < 0.5)
    {
        return float4(0.02, 0.025, 0.035, 1);
    }
    float3 farPoint  = unproject(input.uv, 1, frame.inverseViewProjection);
    float3 direction = normalize(farPoint - frame.cameraPosition.xyz);
    float3 color = radianceMap.SampleLevel(environmentSampler, environmentUv(direction), 0).rgb * frame.lightColor.w;
    return float4(color, 1);
}
