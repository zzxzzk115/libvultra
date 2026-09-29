struct MeshPayload
{
    uint count;
};

struct Vertex
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[shader("amplification")]
[numthreads(1, 1, 1)]
void taskMain()
{
    MeshPayload payload;
    payload.count = 3;
    DispatchMesh(payload.count, 1, 1, payload);
}

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void meshMain(uint3 group : SV_GroupID, in payload MeshPayload payload,
              out vertices Vertex vertices[3], out indices uint3 indices[1])
{
    SetMeshOutputCounts(3, 1);
    // Same three depth-offset triangles as dev. Vultra uses a negative-height Vulkan viewport.
    float depth = float(group.x);
    vertices[0].position = float4(0, 0.5, depth, 1);
    vertices[1].position = float4(-0.5, -0.5, depth, 1);
    vertices[2].position = float4(0.5, -0.5, depth, 1);
    vertices[0].color = float4(0, 1, 0, 1);
    vertices[1].color = float4(0, 0, 1, 1);
    vertices[2].color = float4(1, 0, 0, 1);
    indices[0] = uint3(0, 1, 2);
}

[shader("fragment")]
float4 fragmentMain(Vertex input) : SV_Target
{
    return input.color;
}
