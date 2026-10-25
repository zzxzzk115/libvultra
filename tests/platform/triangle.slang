struct Vertex
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
Vertex vertexMain(uint index : SV_VertexID)
{
    const float2 positions[3] = {float2(-0.75, -0.65), float2(0.0, 0.75), float2(0.75, -0.65)};
    const float3 colors[3] = {float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1)};
    Vertex result;
    result.position = float4(positions[index], 0.5, 1.0);
    result.color = colors[index];
    return result;
}

[shader("fragment")]
float4 fragmentMain(Vertex vertex) : SV_Target0
{
    return float4(vertex.color, 1.0);
}
