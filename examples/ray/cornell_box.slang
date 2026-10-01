#include "ray_resources.slangh"
#include "lib/color.slangh"

// Same direct-light approximation as dev: trace to the emissive quad's centroid.
// This is not a path tracer or a sampled area-light integrator.
float3 neutralToneMap(float3 color)
{
    float x = min(color.r, min(color.g, color.b));
    color -= x < 0.08 ? x - 6.25 * x * x : 0.04;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < 0.76)
    {
        return color;
    }
    float newPeak = 1 - 0.24 * 0.24 / (peak + 0.24 - 0.76);
    color *= newPeak / peak;
    float g = 1 - 1 / (0.15 * (peak - newPeak) + 1);
    return lerp(color, newPeak.xxx, g);
}

[shader("raygeneration")]
void raygenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    float2 ndc = (float2(pixel) + 0.5) / float2(DispatchRaysDimensions().xy) * 2 - 1;
    float4 world = mul(parameters.inverseViewProjection, float4(ndc.x, -ndc.y, 1, 1));
    RayDesc ray;
    ray.Origin = parameters.cameraPosition.xyz;
    ray.Direction = normalize(world.xyz / world.w - ray.Origin);
    ray.TMin = 0.001;
    ray.TMax = 100;
    ColorPayload payload;
    payload.color = 0;
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 255, 0, 0, 0, ray, payload);
    outputImage[pixel] = float4(payload.color, 1);
}

[shader("miss")]
void shadowMissMain(inout ShadowPayload payload)
{
    payload.blocked = false;
}

[shader("closesthit")]
void closestHitMain(inout ColorPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    uint first = PrimitiveIndex() * 3;
    RayVertex a = vertices[first];
    RayVertex b = vertices[first + 1];
    RayVertex c = vertices[first + 2];
    if (any(a.emission.rgb > 0))
    {
        payload.color = a.emission.rgb;
        return;
    }
    float3 bary = float3(1 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
    float3 position = a.position.xyz * bary.x + b.position.xyz * bary.y + c.position.xyz * bary.z;
    float3 normal = normalize(a.normal.xyz * bary.x + b.normal.xyz * bary.y + c.normal.xyz * bary.z);
    float3 toLight = parameters.lightPositionArea.xyz - position;
    float distance = length(toLight);
    float3 direction = toLight / distance;
    RayDesc ray;
    ray.Origin = position + normal * 1e-5;
    ray.Direction = direction;
    ray.TMin = 1e-5;
    ray.TMax = distance - 1e-3;
    ShadowPayload shadow;
    shadow.blocked = true;
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             255, 0, 0, 1, ray, shadow);
    float3 lighting = 0;
    if (!shadow.blocked)
    {
        lighting = parameters.lightEmission.rgb * max(dot(normal, direction), 0)
                 * max(dot(parameters.lightNormal.xyz, -direction), 0)
                 * parameters.lightPositionArea.w / (distance * distance);
    }
    float3 baseColor = a.color.rgb * bary.x + b.color.rgb * bary.y + c.color.rgb * bary.z;
    payload.color = linearToSrgb(neutralToneMap(baseColor * lighting));
}
