#include "lib/ray_surface.slangh"
#include "lib/texture.slangh"
#include "lib/environment_sampling.slangh"

uint hashRandom(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    return value ^ (value >> 16);
}

float random(inout uint state)
{
    state = hashRandom(state + 0x9e3779b9);
    // A 23-bit midpoint stays strictly inside (0, 1) in float32, including the largest integer.
    return (float(state >> 9) + 0.5) * (1.0 / 8388608.0);
}

float3 random3(inout uint state)
{
    float x = random(state);
    float y = random(state);
    float z = random(state);
    return float3(x, y, z);
}

float misWeight(float sampledPdf, float otherPdf)
{
    if (sampledPdf <= 0)
    {
        return 0;
    }
    float ratio = otherPdf / sampledPdf;
    return 1 / (1 + ratio * ratio);
}

float3 environmentRadiance(float3 direction)
{
    return traceEnvironment.SampleLevel(traceEnvironmentSampler, environmentUv(direction), 0).rgb *
           traceFrame.options.x;
}

bool visible(RaySurface surface, float3 direction, float distance, inout uint rays)
{
    if (dot(direction, surface.geometricNormal) <= 0)
    {
        return false;
    }
    float epsilon = traceFrame.options.y;
    RayDesc shadow;
    shadow.Origin = surface.position + surface.geometricNormal * epsilon;
    shadow.Direction = direction;
    shadow.TMin = epsilon;
    shadow.TMax = distance - 2 * epsilon;
    if (shadow.TMax <= shadow.TMin)
    {
        return false;
    }
    ++rays;
    return !traceRay(shadow, true).found;
}

float3 directAnalytic(RaySurface surface, OpenPBR_PreparedBsdf bsdf, inout uint state, inout uint rays)
{
    uint count = traceFrame.counts.z;
    if (count == 0)
    {
        return float3(0);
    }
    TraceLight light = traceLights[min(uint(random(state) * count), count - 1)];
    float3 direction = light.directionKind.xyz;
    float attenuation = 1;
    float distance = 1e30;
    if (uint(light.directionKind.w) != 0)
    {
        float3 delta = light.positionRange.xyz - surface.position;
        float distanceSquared = dot(delta, delta);
        distance = sqrt(distanceSquared);
        if (distance <= traceFrame.options.y)
        {
            return float3(0);
        }
        direction = delta / distance;
        float cutoff = saturate(1 - pow(distance / light.positionRange.w, 4));
        attenuation = cutoff * cutoff / distanceSquared;
        if (uint(light.directionKind.w) == 2)
        {
            attenuation *= smoothstep(light.cones.y, light.cones.x, dot(direction, light.directionKind.xyz));
        }
    }
    if (attenuation == 0 || !visible(surface, direction, distance, rays))
    {
        return float3(0);
    }
    // Delta lights have no competing BSDF sampling technique. Uniform light selection has probability 1/count.
    return openpbr_get_sum_of_diffuse_specular(openpbr_eval(bsdf, direction)) *
           light.colorIntensity.rgb * light.colorIntensity.w * attenuation * count;
}

float3 directEnvironment(RaySurface surface, OpenPBR_PreparedBsdf bsdf, inout uint state, inout uint rays)
{
    if (traceFrame.options.x == 0)
    {
        return float3(0);
    }
    float technique = random(state);
    float cell = random(state);
    float chooseAlias = random(state);
    float longitude = random(state);
    float latitude = random(state);
    float3 direction = sampleEnvironment(technique, cell, chooseAlias, longitude, latitude);
    if (!visible(surface, direction, 1e30, rays))
    {
        return float3(0);
    }
    float pdf = environmentPdf(direction);
    return openpbr_get_sum_of_diffuse_specular(openpbr_eval(bsdf, direction)) * environmentRadiance(direction) *
           misWeight(pdf, openpbr_pdf(bsdf, direction)) / pdf;
}

float emitterPdf(RaySurface light, float3 from)
{
    float3 delta = light.position - from;
    float distanceSquared = dot(delta, delta);
    float cosine = abs(dot(light.geometricNormal, normalize(delta)));
    if (cosine == 0 || light.area == 0 || traceFrame.counts.w == 0)
    {
        return 0;
    }
    return distanceSquared / (cosine * light.area * traceFrame.counts.w);
}

float3 directEmitter(RaySurface surface, OpenPBR_PreparedBsdf bsdf, inout uint state, inout uint rays)
{
    uint count = traceFrame.counts.w;
    if (count == 0)
    {
        return float3(0);
    }
    uint2 emitter = traceEmitters[min(uint(random(state) * count), count - 1)];
    float root = sqrt(random(state));
    float other = random(state);
    RayHit hit;
    hit.primitive = emitter.x;
    hit.triangle = emitter.y;
    hit.barycentrics = float2(root * (1 - other), root * other);
    hit.frontFace = true;
    RaySurface light = raySurface(hit);
    float3 delta = light.position - surface.position;
    float distance = length(delta);
    if (distance <= traceFrame.options.y)
    {
        return float3(0);
    }
    float3 direction = delta / distance;
    bool facing = dot(light.geometricNormal, -direction) > 0;
    if (!acceptRayCandidate(hit.primitive, hit.triangle, hit.barycentrics, facing))
    {
        return float3(0);
    }
    float pdf = emitterPdf(light, surface.position);
    if (pdf <= 0 || !visible(surface, direction, distance, rays))
    {
        return float3(0);
    }
    return openpbr_get_sum_of_diffuse_specular(openpbr_eval(bsdf, direction)) * light.emission *
           misWeight(pdf, openpbr_pdf(bsdf, direction)) / pdf;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void traceMain(uint3 thread : SV_DispatchThreadID)
{
    uint width;
    uint height;
    traceHdr.GetDimensions(width, height);
    uint2 pixel = thread.xy;
    if (pixel.x >= width || pixel.y >= height)
    {
        return;
    }
    uint sample = traceFrame.counts.x;
    uint state = hashRandom(traceFrame.counts.y ^ hashRandom(pixel.x + pixel.y * width) ^ hashRandom(sample));
    float2 jitter = float2(random(state), random(state));
    float2 ndc = (float2(pixel) + jitter) / float2(width, height) * 2 - 1;
    // RenderCamera uses a right-handed, zero-to-one projection: top-left pixels correspond to positive clip-space Y; Z is in [0, 1].
    float4 world = mul(traceFrame.inverseViewProjection, float4(ndc.x, -ndc.y, 0, 1));
    RayDesc ray;
    ray.Origin = traceFrame.eye.xyz;
    ray.Direction = normalize(world.xyz / world.w - ray.Origin);
    ray.TMin = traceFrame.options.y;
    ray.TMax = 1e30;
    float3 radiance = float3(0);
    float3 throughput = float3(1);
    float3 previousPosition = ray.Origin;
    float previousPdf = 0;
    uint rays = 0;
    uint bounce = 0;
    float3 albedo = float3(0);
    float3 normal = float3(0);
    float depth = 0;
    float2 motion = float2(0);
    for (;;)
    {
        ++rays;
        RayHit hit = traceRay(ray, false);
        if (!hit.found)
        {
            float weight = bounce == 0 ? 1 : misWeight(previousPdf, environmentPdf(ray.Direction));
            radiance += throughput * environmentRadiance(ray.Direction) * weight;
            break;
        }
        RaySurface surface = raySurface(hit);
        if (bounce == 0)
        {
            albedo = surface.albedo;
            normal = surface.normal;
            depth = -mul(traceFrame.view, float4(surface.position, 1)).z;
            float4 current = mul(traceFrame.viewProjection, float4(surface.position, 1));
            float4 previousWorld = mul(tracePreviousTransforms[hit.primitive].model, float4(surface.objectPosition, 1));
            float4 previous = mul(traceFrame.previousViewProjection, previousWorld);
            if (previous.w > 0 && current.w > 0)
            {
                motion = (previous.xy / previous.w - current.xy / current.w) * float2(0.5, -0.5) * float2(width, height);
            }
        }
        float emissionWeight = bounce == 0 ? 1 : misWeight(previousPdf, emitterPdf(surface, previousPosition));
        radiance += throughput * surface.emission * emissionWeight;
        OpenPBR_PreparedBsdf bsdf = prepareOpenPbr(surface.material, surface.normal, -ray.Direction);
        radiance += throughput * (directAnalytic(surface, bsdf, state, rays) +
                                  directEnvironment(surface, bsdf, state, rays) + directEmitter(surface, bsdf, state, rays));
        float3 direction;
        OpenPBR_DiffuseSpecular weight;
        float pdf;
        OpenPBR_BsdfLobeType lobe;
        openpbr_sample(bsdf, random3(state), direction, weight, pdf, lobe);
        if (pdf <= 0 || dot(direction, surface.geometricNormal) <= 0)
        {
            break;
        }
        // Upstream sample weight already includes BSDF*cosine / the combined lobe PDF.
        throughput *= openpbr_get_sum_of_diffuse_specular(weight);
        float maximum = max(throughput.x, max(throughput.y, throughput.z));
        if (maximum <= 0)
        {
            break;
        }
        ++bounce;
        if (bounce >= 3)
        {
            float survival = min(maximum, 0.95);
            if (random(state) >= survival)
            {
                break;
            }
            throughput /= survival;
        }
        previousPosition = surface.position;
        previousPdf = pdf;
        ray.Origin = surface.position + surface.geometricNormal * traceFrame.options.y;
        ray.Direction = direction;
        ray.TMin = traceFrame.options.y;
    }
    float3 mean = (traceRadiance[pixel].rgb * sample + radiance) / float(sample + 1);
    traceRadiance[pixel] = float4(mean, 1);
    traceHdr[pixel] = float4(mean, 1);
    traceAlbedo[pixel] = float4(albedo, 1);
    traceNormal[pixel] = float4(normal, 1);
    traceDepth[pixel] = float4(depth.xxx, 1);
    traceMotion[pixel] = float4(motion, 0, 1);
    traceSamples[pixel] = float4(float(sample + 1).xxx, 1);
    traceRays[pixel] = float4(traceRays[pixel].rgb + float(rays).xxx, 1);
}
