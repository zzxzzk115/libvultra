// Convert packed opaque depth for target-eye alpha testing. RGB stays in its
// existing HDR attachment; blending preserves its depth channel.
[[vk::binding(0, 0)]] Texture2D<float4> opaqueDepth;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")] VertexOutput screenVertex(uint id : SV_VertexID)
{
    VertexOutput result;
    result.position = float4(float2((id << 1) & 2, id & 2) * 2 - 1, 0, 1);
    return result;
}

    [shader("fragment")] float depthMain(VertexOutput input) :
    SV_Depth
{
    float depth = opaqueDepth.Load(int3(int2(input.position.xy), 0)).a;
    return depth >= 0 && depth <= 0.5 ? depth * 2 : 1.0;
}
