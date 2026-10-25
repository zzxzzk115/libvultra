[[vk::binding(0, 0)]] RWStructuredBuffer<uint> pixels;

uint4 pattern(uint2 position)
{
    return uint4(position.x * 4, position.y * 4, (position.x ^ position.y) * 4, 255);
}

[shader("compute")][numthreads(8, 8, 1)] void computeMain(uint3 id : SV_DispatchThreadID)
{
    uint4 value              = pattern(id.xy);
    pixels[id.y * 64 + id.x] = value.x | (value.y << 8) | (value.z << 16) | (value.w << 24);
}

    [shader("vertex")] float4 fallbackVertex(uint index : SV_VertexID) :
    SV_Position
{
    float2 uv = float2((index << 1) & 2, index & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

[shader("fragment")] float4 fallbackFragment(float4 position : SV_Position) :
    SV_Target0
{
    return float4(pattern(uint2(position.xy))) / 255;
}
