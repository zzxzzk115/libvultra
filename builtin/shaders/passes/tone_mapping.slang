#include "lib/color.slangh"
#include "resources/frame_block.slangh"
#include "resources/fullscreen_vertex.slangh"

[shader("fragment")] float4 fragmentMain(ScreenVertex input) :
    SV_Target
{
    float3 color = hdrColor.Load(int3(int2(input.position.xy), 0)).rgb;
    if (frame.overrides.w < 0.5)
    {
        color = toneMapAces(color * exp2(frame.options.z));
    }
    // Meshlet colors are a display palette; exposure and tone mapping must not alter it.
    // Float output stays linear for XR. Desktop UNORM output receives one display transfer.
    return float4(frame.overrides.z > 0.5 ? linearToSrgb(color) : color, 1);
}
