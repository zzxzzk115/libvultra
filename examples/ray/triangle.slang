#include "lib/color.slangh"
#include "ray_resources.slangh"

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    float2 ndc = (float2(pixel) + 0.5) / float2(DispatchRaysDimensions().xy) * 2 - 1;
    RayDesc ray;
    ray.Origin = float3(ndc.x, -ndc.y, -2);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 100;
    ColorPayload payload;
    payload.color = 0;
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 255, 0, 0, 0, ray, payload);
    outputImage[pixel] = float4(payload.color, 1);
}

[shader("closesthit")]
void closestHitMain(inout ColorPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    uint first = PrimitiveIndex() * 3;
    float3 bary = float3(1 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
    float3 color = vertices[first].color.rgb * bary.x
                 + vertices[first + 1].color.rgb * bary.y
                 + vertices[first + 2].color.rgb * bary.z;
    // RayTracingApp copies display-encoded output to its UNORM swapchain.
    payload.color = linearToSrgb(max(color, 0));
}
