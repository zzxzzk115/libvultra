// Ported from the PVW companion renderer.
#if !defined(VULTRA_OPENPBR_REFERENCE) && !defined(VULTRA_OPENPBR_OPAQUE_SURFACE)
// The visibility material mapping does not supply transmission, subsurface or dispersion.
#define VULTRA_OPENPBR_OPAQUE_SURFACE 1
#endif
#include "lib/surface_lighting.slangh"
#include "lib/sky_color.slangh"
#include "resources/fullscreen_vertex.slangh"
#include "resources/mesh_vertex.slangh"

#include "resources/visibility_scene.slangh"
#ifdef VULTRA_VISIBILITY_COLOR_INPUT
// Vulkan input attachment reads return four components, including for RG32_UINT.
[[vk::binding(5, 1), vk::input_attachment_index(0)]] SubpassInput<uint4> visibility;
#else
[[vk::binding(5, 1)]] Texture2D<uint2> visibility;
#endif
[[vk::binding(6, 1), vk::image_format("rgba16f")]] RWTexture2D<float4> hdrOutput;
// One demand value per 2x2 source quad: 0 = quarter, 1 = half, 2 = full rate.
[[vk::binding(7, 1)]] Texture2D<uint> shadingDemand;
[[vk::binding(8, 1), vk::image_format("r8ui")]] Texture2D<uint> repairMask;
#ifdef VULTRA_CLUSTERED_LIGHTING
[[vk::binding(9, 1)]] StructuredBuffer<uint2> lightClusters;
#endif
struct VisibilityPush { float4 foveation; };
[[vk::push_constant]] VisibilityPush push;

uint2 visibilityExtent()
{
#ifdef VULTRA_VISIBILITY_COLOR_INPUT
    return asuint(push.foveation.zw);
#else
    uint2 extent;
    visibility.GetDimensions(extent.x, extent.y);
    return extent;
#endif
}

uint2 loadFragmentVisibility(uint2 pixel)
{
#ifdef VULTRA_VISIBILITY_COLOR_INPUT
    return visibility.SubpassLoad().xy;
#else
    return visibility.Load(int3(pixel, 0));
#endif
}

float4 sampleMaterial(uint index, float2 uv, float2 dx, float2 dy)
{
    return materialTextures[NonUniformResourceIndex(index)].SampleGrad(
        materialSamplers[NonUniformResourceIndex(index)], uv, dx, dy);
}

float4 shadeVisibility(uint2 pixel, uint2 id, float2 position, float2 footprint, bool materialBatch)
{
    uint instance = id.x - 1;
    uint2 primitive = primitives[instance + 1];
    uint first = primitive.x + (id.y & 0x7fffffffu) * 3;
    MeshVertex a;
    MeshVertex b;
    MeshVertex c;
    loadVisibilityTriangle(uint3(indices[first], indices[first + 1], indices[first + 2]), a, b, c);
    VisibilityTransform transform = transforms[instance];
    float3 wa = mul(transform.model, float4(a.position, 1)).xyz;
    float3 wb = mul(transform.model, float4(b.position, 1)).xyz;
    float3 wc = mul(transform.model, float4(c.position, 1)).xyz;
    float4 ca = mul(frame.viewProjection, float4(wa, 1));
    float4 cb = mul(frame.viewProjection, float4(wb, 1));
    float4 cc = mul(frame.viewProjection, float4(wc, 1));
    const uint2 extent = visibilityExtent();
    const uint width = extent.x;
    const uint height = extent.y;
    float2 step = float2(2.0 / width, -2.0 / height);
    float2 clip = position * step + float2(-1, 1);
    float3 x = float3(ca.x, cb.x, cc.x);
    float3 y = float3(ca.y, cb.y, cc.y);
    float3 w = float3(ca.w, cb.w, cc.w);
    // Homogeneous solve avoids dividing individual vertices by w (including triangles crossing the eye plane).
    float3 numerator = cross(x - clip.x * w, y - clip.y * w);
    float3 bary = numerator / dot(numerator, 1.0.xxx);
    float3 nx = cross(y, w) * step.x;
    float3 ny = cross(w, x) * step.y;
    // Evaluate the same triangle at both ends of this pixel's 2x2 derivative quad.
    // Reading adjacent visibility pixels would cross materials/silhouettes and corrupt mip selection.
    float3 left = numerator - float(pixel.x & 1) * nx;
    float3 right = left + nx;
    float3 top = numerator - float(pixel.y & 1) * ny;
    float3 bottom = top + ny;
    float3 bx = right / dot(right, 1.0.xxx) - left / dot(left, 1.0.xxx);
    float3 by = bottom / dot(bottom, 1.0.xxx) - top / dot(top, 1.0.xxx);
    float2 uv = a.uv * bary.x + b.uv * bary.y + c.uv * bary.z;
    float2 dx = a.uv * bx.x + b.uv * bx.y + c.uv * bx.z;
    float2 dy = a.uv * by.x + b.uv * by.y + c.uv * by.z;
    dx *= footprint.x;
    dy *= footprint.y;
    float4 color = a.color * bary.x + b.color * bary.y + c.color * bary.z;
    float3 normal = normalize(mul(transform.normal, float4(a.normal * bary.x + b.normal * bary.y + c.normal * bary.z, 0)).xyz);
    float4 tangent = a.tangent * bary.x + b.tangent * bary.y + c.tangent * bary.z;
    tangent.xyz = mul(transform.model, float4(tangent.xyz, 0)).xyz;
    tangent.w *= transform.properties.x;
    VisibilityMaterial material = materials[primitive.y];
    uint slot = materialBatch ? 0 : primitive.y * 7;
    SurfaceData surface;
    surface.worldPosition = wa * bary.x + wb * bary.y + wc * bary.z;
    float faceSign = (id.y & 0x80000000u) == 0 ? 1 : -1;
    surface.geometricNormal = normal * faceSign;
    if (material.flags.w >= 0.5)
    {
        float3 t = tangent.xyz - normal * dot(tangent.xyz, normal);
        float lengthSquared = dot(t, t);
        if (lengthSquared >= 1e-12)
        {
            t *= rsqrt(lengthSquared);
            float3 mapped = sampleMaterial(slot + 2, uv, dx, dy).xyz * 2 - 1;
            if (material.flags.w > 1.5)
            {
                mapped.z = sqrt(saturate(1 - dot(mapped.xy, mapped.xy)));
            }
            mapped.xy *= material.surface.w;
            normal = normalize(t * mapped.x + tangent.w * cross(normal, t) * mapped.y + normal * mapped.z);
        }
    }
    surface.normal = normal * faceSign;
    surface.baseColor = sampleMaterial(slot, uv, dx, dy).rgb * material.baseColor.rgb * color.rgb;
    surface.baseWeight = material.flags.y;
    float2 mr = sampleMaterial(slot + 1, uv, dx, dy).bg;
    surface.metallic = saturate(material.surface.x * mr.x);
    surface.roughness = clamp(material.surface.y * mr.y, 0.03, 1);
    if (frame.overrides.x >= 0)
    {
        surface.roughness = clamp(frame.overrides.x, 0.03, 1);
    }
    if (frame.overrides.y >= 0)
    {
        surface.metallic = saturate(frame.overrides.y);
    }
    surface.emission = sampleMaterial(slot + 4, uv, dx, dy).rgb * material.emission.rgb;
    surface.occlusion = lerp(1, sampleMaterial(slot + 3, uv, dx, dy).r, material.emission.w);
    surface.specularColor = material.specular.rgb * sampleMaterial(slot + 6, uv, dx, dy).rgb;
    surface.specularWeight = material.coat.w * sampleMaterial(slot + 5, uv, dx, dy).a;
    surface.specularIor = material.surface.z;
    surface.coatWeight = material.coat.x;
    surface.coatRoughness = material.coat.y;
    surface.coatIor = material.coat.z;
    surface.baseDiffuseRoughness = material.specular.w;
#ifdef VULTRA_CLUSTERED_LIGHTING
    float depth = -mul(frame.view, float4(surface.worldPosition, 1)).z;
    float nearPlane = frame.cameraClip.x;
    float farPlane = frame.cameraClip.y;
    uint2 mask = uint2(0xffffffffu, 0xffffffffu);
    // Out-of-range geometry falls back to the complete list rather than losing illumination.
    if (nearPlane > 0 && farPlane > nearPlane && depth >= nearPlane && depth <= farPlane)
    {
        uint columns = (width + 15) / 16;
        uint rows = (height + 15) / 16;
        uint2 tile = min(uint2(position) / 16, uint2(columns - 1, rows - 1));
        uint slice = min(uint(max(log(depth / nearPlane) / log(farPlane / nearPlane), 0) * 24), 23u);
        mask = lightClusters[(slice * rows + tile.y) * columns + tile.x];
    }
    return float4(shadeSurface(surface, frame.cameraPosition.xyz, mask), 1);
#else
    return float4(shadeSurface(surface), 1);
#endif
}

[shader("fragment")]
float4 fragmentMain(ScreenVertex input) : SV_Target
{
    uint2 pixel = uint2(input.position.xy);
    uint2 id = loadFragmentVisibility(pixel);
    if (id.x == 0)
    {
        const uint2 size = visibilityExtent();
        // Match compute's pixel-center arithmetic, including HDR sky rounding at window edges.
        return skyColor((float2(pixel) + 0.5) / float2(size));
    }
    return shadeVisibility(pixel, id, input.position.xy, 1.0.xx, false);
}

// Fallback for devices whose fixed descriptor limits cannot hold every material at once.
// Every draw binds one material; discard leaves other materials for their own draw.
[shader("fragment")]
float4 fragmentBatchedMain(ScreenVertex input) : SV_Target
{
    uint2 pixel = uint2(input.position.xy);
    uint2 id = loadFragmentVisibility(pixel);
    uint materialIndex = asuint(push.foveation.x);
    if (id.x == 0)
    {
        if (materialIndex != 0) discard;
        const uint2 size = visibilityExtent();
        return skyColor((float2(pixel) + 0.5) / float2(size));
    }
    if (primitives[id.x].y != materialIndex) discard;
    return shadeVisibility(pixel, id, input.position.xy, 1.0.xx, true);
}

#ifndef VULTRA_VISIBILITY_COLOR_INPUT
[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    visibility.GetDimensions(size.x, size.y);
    if (any(tid.xy >= size)) return;
    uint2 id = visibility.Load(int3(tid.xy, 0));
    hdrOutput[tid.xy] = id.x != 0 ? shadeVisibility(tid.xy, id, float2(tid.xy) + 0.5, 1.0.xx, false)
                                  : skyColor((float2(tid.xy) + 0.5) / float2(size));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void maskedMain(uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    visibility.GetDimensions(size.x, size.y);
    if (any(tid.xy >= size)) return;
    // This entry binds a full-resolution hole mask to the existing demand slot.
    // Preserve sky initialization for unmasked pixels without a preceding full-screen write.
    uint2 id = 0;
    if (repairMask.Load(int3(tid.xy, 0)) != 0) id = visibility.Load(int3(tid.xy, 0));
    hdrOutput[tid.xy] = id.x != 0 ? shadeVisibility(tid.xy, id, float2(tid.xy) + 0.5, 1.0.xx, false)
                                  : skyColor((float2(tid.xy) + 0.5) / float2(size));
}

// Equivalent masked resolve for devices that bind one material per dispatch.
[shader("compute")]
[numthreads(8, 8, 1)]
void maskedBatchedMain(uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    visibility.GetDimensions(size.x, size.y);
    if (any(tid.xy >= size)) return;
    const uint materialIndex = asuint(push.foveation.x);
    uint2 id = 0;
    if (repairMask.Load(int3(tid.xy, 0)) != 0) id = visibility.Load(int3(tid.xy, 0));
    if (id.x != 0 && primitives[id.x].y == materialIndex)
    {
        hdrOutput[tid.xy] = shadeVisibility(tid.xy, id, float2(tid.xy) + 0.5, 1.0.xx, true);
    }
    else if (materialIndex == 0)
    {
        hdrOutput[tid.xy] = skyColor((float2(tid.xy) + 0.5) / float2(size));
    }
}

// GPC 2025, Fuller/Hammer, slides 14-22: compact primaries within 16x16 tiles.
// Here each lane classifies a 2x2 quad; wave scans pack up to 256 local commands into shared memory.
// No screen-sized sample list or per-pixel append atomics; sky is resolved during classification.
groupshared uint commands[256];
groupshared uint waveOffsets[64];
groupshared uint commandCount;

uint command(uint2 local, uint mask) { return local.x | (local.y << 4) | (mask << 8); }

[shader("compute")]
[numthreads(64, 1, 1)]
void foveatedMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint2 size;
    visibility.GetDimensions(size.x, size.y);
    uint2 origin = group.xy * 16;
    uint2 local = uint2(lane & 7, lane >> 3) * 2;
    uint2 pixel = origin + local;
    uint2 ids[4];
    [unroll] for (uint i = 0; i < 4; ++i)
    {
        uint2 p = pixel + uint2(i & 1, i >> 1);
        ids[i] = all(p < size) ? visibility.Load(int3(p, 0)) : uint2(0, 0);
        if (all(p < size) && ids[i].x == 0)
            hdrOutput[p] = skyColor((float2(p) + 0.5) / float2(size));
    }
    // The nearest pixel center must be outside the fovea: no coarse group straddles its inner boundary.
    float2 gaze = push.foveation.xy * float2(size);
    float2 nearest = clamp(gaze, float2(pixel) + 0.5, float2(pixel) + 1.5);
    float radius = length(nearest - gaze) / float(min(size.x, size.y));
    bool halfRate = radius >= push.foveation.z;
    bool quarterRate = radius >= push.foveation.w;
    if (push.foveation.x < 0)
    {
        uint demand = all(pixel < size) ? shadingDemand.Load(int3(pixel / 2, 0)) : 2;
        halfRate = demand < 2;
        quarterRate = demand == 0;
    }
    uint work[4];
    uint count = 0;
    if (quarterRate && ids[0].x != 0 && all(ids[0] == ids[1]) && all(ids[0] == ids[2]) && all(ids[0] == ids[3]))
    {
        work[count++] = command(local, 15);
    }
    else
    {
        [unroll] for (uint row = 0; row < 2; ++row)
        {
            uint a = row * 2;
            if (halfRate && ids[a].x != 0 && all(ids[a] == ids[a + 1]))
            {
                work[count++] = command(local + uint2(0, row), 3);
            }
            else
            {
                [unroll] for (uint col = 0; col < 2; ++col)
                {
                    if (ids[a + col].x != 0) work[count++] = command(local + uint2(col, row), 1);
                }
            }
        }
    }
    uint waveSize = WaveGetLaneCount();
    uint wave = lane / waveSize;
    uint offset = WavePrefixSum(count);
    uint sum = WaveActiveSum(count);
    if (WaveIsFirstLane()) waveOffsets[wave] = sum;
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0)
    {
        uint total = 0;
        for (uint i = 0; i < (64 + waveSize - 1) / waveSize; ++i)
        {
            uint n = waveOffsets[i];
            waveOffsets[i] = total;
            total += n;
        }
        commandCount = total;
    }
    GroupMemoryBarrierWithGroupSync();
    offset += waveOffsets[wave];
    for (uint i = 0; i < count; ++i) commands[offset + i] = work[i];
    GroupMemoryBarrierWithGroupSync();
    for (uint index = lane; index < commandCount; index += 64)
    {
        uint data = commands[index];
        uint2 p = origin + uint2(data & 15, (data >> 4) & 15);
        uint mask = data >> 8;
        float2 footprint = float2((mask & 2) != 0 ? 2 : 1, (mask & 4) != 0 ? 2 : 1);
        // Shade the coarse pixel center (slides 27-29); explicit gradients account for its footprint.
        float4 color = shadeVisibility(p, visibility.Load(int3(p, 0)), float2(p) + footprint * 0.5, footprint, false);
        hdrOutput[p] = color;
        if ((mask & 2) != 0) hdrOutput[p + uint2(1, 0)] = color;
        if ((mask & 4) != 0) hdrOutput[p + uint2(0, 1)] = color;
        if ((mask & 8) != 0) hdrOutput[p + uint2(1, 1)] = color;
    }
}
#endif
