#include "resources/fullscreen_vertex.slangh"
#include "resources/frame_block.slangh"
#include "resources/gbuffer.slangh"

[shader("fragment")] float4 fragmentMain(ScreenVertex input) : SV_Target
{
    int2 pixel = int2(input.position.xy);
    float2 depthMetallic = gbufferPositionMetallic.Load(int3(pixel, 0)).xy;
    if (depthMetallic.y < 0)
        discard;
    uint width, height;
    gbufferPositionMetallic.GetDimensions(width, height);
    float3 position = reconstructGBufferPosition(pixel, depthMetallic.x, uint2(width, height),
                                                 frame.inverseViewProjection);
    return float4(position * 0.25 + 0.5, 1);
}
