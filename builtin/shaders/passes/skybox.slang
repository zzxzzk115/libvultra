import vultra.gbuffer;
import vultra.material;

#include "lib/space.slangh"
#include "resources/frame_block.slangh"
#include "resources/fullscreen_vertex.slangh"

// The sky seen through a screen position, or the plain background when the skybox is off.
float4 skyColor(ScreenVertex input)
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

[shader("fragment")] float4 fragmentMain(ScreenVertex input) : SV_Target
{
    return skyColor(input);
}

// The G-buffer variant: the sky's colour and its motion under the camera's movement; no surface layers.
struct GBufferOutput
{
    float4 hdr : SV_Target0;
    float2 normal : SV_Target1;
    float4 albedo : SV_Target2;
    VultraMaterialLayer material : SV_Target3;
    VultraEmissionLayer emission : SV_Target4;
    float2 motion : SV_Target5;
#ifndef VULTRA_GBUFFER_COMPACT
    float4 coat : SV_Target6;
    float4 specular : SV_Target7;
#endif
};

[shader("fragment")] GBufferOutput fragmentGBuffer(ScreenVertex input)
{
    GBufferOutput output;
    output.hdr      = skyColor(input);
    output.normal   = 0;
    output.albedo   = 0;
    output.material = 0;
    output.emission = 0;
#ifndef VULTRA_GBUFFER_COMPACT
    output.coat     = 0;
    output.specular = 0;
#endif
    float3 farPoint = unproject(input.uv, 1, frame.inverseViewProjection);
    float4 previous = mul(camera.previousViewProjection, float4(farPoint, 1));
    output.motion   = (previous.xy / previous.w - (uvToClip(input.uv) - cameraJitterNdc(camera))) * float2(0.5, -0.5);
    return output;
}
