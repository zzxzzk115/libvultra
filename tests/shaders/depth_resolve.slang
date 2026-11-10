struct DrawConstants
{
    float depth;
    uint coverage;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw;

[shader("vertex")]
float4 vertexMain(uint id : SV_VertexID) : SV_Position
{
    return float4(float2((id << 1) & 2, id & 2) * 2 - 1, 0, 1);
}

struct DepthSample
{
    float depth : SV_Depth;
    uint coverage : SV_Coverage;
};

[shader("fragment")]
DepthSample fragmentMain()
{
    DepthSample result;
    result.depth = draw.depth;
    result.coverage = draw.coverage;
    return result;
}

[shader("fragment")]
uint stencilMain() : SV_Coverage
{
    return draw.coverage;
}
