#include "triangle.slangh"

struct MeshPayload
{
    uint vertexCount;
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
    payload.vertexCount = 3;
    DispatchMesh(1, 1, 1, payload);
}

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void meshMain(in payload MeshPayload payload,
              out vertices Vertex vertices[3], out indices uint3 indices[1])
{
    SetMeshOutputCounts(payload.vertexCount, 1);
    for (uint i = 0; i < payload.vertexCount; ++i)
    {
        vertices[i].position = float4(kTrianglePositions[i], 1);
        vertices[i].color = float4(kTriangleColors[i], 1);
    }
    indices[0] = uint3(0, 1, 2);
}

[shader("fragment")]
float4 fragmentMain(Vertex input) : SV_Target
{
    return input.color;
}
