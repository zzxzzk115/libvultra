#include "resources/openpbr_luts.slangh"

[[vk::binding(12, 0), vk::image_format("rgba32f")]] RWTexture2D<float4> differences;

// Freeze the previous metadata-driven sampler independently of the shared layout.
float4 referenceLoad(uint4 table, uint3 position)
{
    uint index = table.x + (position.z * table.z + position.y) * table.y + position.x;
    return openPbrLuts.Load(int3(index % 32, index / 32, 0));
}

float4 referenceSlice(uint4 table, float2 uv, uint slice)
{
    float2 pixel = clamp(uv * float2(table.yz) - 0.5, 0, float2(table.yz) - 1);
    uint2 lower = uint2(pixel);
    uint2 upper = min(lower + 1, table.yz - 1);
    float2 fraction = pixel - float2(lower);
    float4 row0 = lerp(referenceLoad(table, uint3(lower, slice)),
                      referenceLoad(table, uint3(upper.x, lower.y, slice)), fraction.x);
    float4 row1 = lerp(referenceLoad(table, uint3(lower.x, upper.y, slice)),
                      referenceLoad(table, uint3(upper, slice)), fraction.x);
    return lerp(row0, row1, fraction.y);
}

float4 referenceSample(uint4 table, float3 uv)
{
    float slice = clamp(uv.z * table.w - 0.5, 0, float(table.w - 1));
    uint lower = uint(slice);
    uint upper = min(lower + 1, table.w - 1);
    return lerp(referenceSlice(table, uv.xy, lower), referenceSlice(table, uv.xy, upper), slice - lower);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint id = thread.x / 4096;
    uint sample = thread.x % 4096;
    const float coordinates[8] = {-0.25, 0, 1.0 / 64, 0.371, 0.5, 63.0 / 64, 1, 1.25};
    float3 uv = float3(coordinates[sample & 7], coordinates[(sample >> 3) & 7], coordinates[(sample >> 6) & 7]);
    if (sample >= 512)
    {
        uint3 bits = uint3(sample * 1664525u + 1013904223u,
                           sample * 22695477u + 1u,
                           sample * 1103515245u + 12345u);
        uv = float3(bits & 16383u) / 8192.0 - 0.5;
    }
    uint4 metadata = uint4(openPbrLuts.Load(int3(id, 0, 0)));
    bool volume = id == OpenPBR_LutId_IdealDielectricEnergyComplement ||
                  id == OpenPBR_LutId_OpaqueDielectricEnergyComplement;
    float4 actual = volume ? sampleOpenPbrLut3D(int(id), uv) : sampleOpenPbrLut2D(int(id), uv.xy);
    float4 reference = volume ? referenceSample(metadata, uv) : referenceSlice(metadata, uv.xy, 0);
    differences[uint2(sample, id)] = any(metadata != openPbrLutRegions[id]) ? float4(1) : actual - reference;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void benchmarkMain(uint3 thread : SV_DispatchThreadID)
{
    float4 sum = 0;
    for (uint i = 0; i < 128; ++i)
    {
        uint id = (thread.x + i) % 7;
        uint3 bits = uint3(thread.x * 1664525u + i * 1013904223u,
                           thread.x * 22695477u + i * 1103515245u,
                           thread.x * 1103515245u + i * 12345u);
        float3 uv = float3(bits & 16383u) / 16383.0;
        bool volume = id == OpenPBR_LutId_IdealDielectricEnergyComplement ||
                      id == OpenPBR_LutId_OpaqueDielectricEnergyComplement;
        sum += volume ? sampleOpenPbrLut3D(int(id), uv) : sampleOpenPbrLut2D(int(id), uv.xy);
    }
    differences[uint2(thread.x % 4096, thread.x / 4096)] = sum;
}
