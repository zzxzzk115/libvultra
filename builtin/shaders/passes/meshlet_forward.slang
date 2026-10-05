#include "lib/color.slangh"
#include "lib/meshlet.slangh"
#include "resources/meshlet_data.slangh"
#include "passes/forward.slang"

struct MeshletPayload
{
    uint visible[kMeshletTaskSize];
};

groupshared MeshletPayload taskPayload;
groupshared uint visibleCount;

[shader("amplification")]
[numthreads(kMeshletTaskSize, 1, 1)]
void taskMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    if (lane == 0)
    {
        visibleCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    uint localIndex = group.x * kMeshletTaskSize + lane;
    // Every lane reaches the barriers, including the final partial workgroup.
    if (localIndex < material.meshlets.y)
    {
        uint index = material.meshlets.x + localIndex;
        PrimitiveTransform instance = primitiveTransforms[material.instance.x];
        float4 bounds = meshlets[index].bounds;
        bounds.xyz = mul(instance.model, float4(bounds.xyz, 1)).xyz;
        bounds.w *= instance.properties.y;
        bool visible = material.meshlets.z == 0 || meshletInsideFrustum(bounds, frame.viewProjection);
        if (visible)
        {
            uint slot;
            InterlockedAdd(visibleCount, 1, slot);
            taskPayload.visible[slot] = index;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(visibleCount, 1, 1, taskPayload);
}

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void meshMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex, in payload MeshletPayload payload,
              out vertices SurfaceVertex vertices[kMeshletVertices], out indices uint3 triangles[kMeshletTriangles])
{
    uint index = payload.visible[group.x];
    Meshlet meshlet = meshlets[index];
    SetMeshOutputCounts(meshlet.geometry.z, meshlet.geometry.w);
    for (uint vertex = lane; vertex < meshlet.geometry.z; vertex += 32)
    {
        MeshVertex input = loadMeshVertex(sceneVertices, meshletVertices[meshlet.geometry.x + vertex]);
        SurfaceVertex output = transformSurfaceVertex(input);
        if (material.meshlets.w != 0)
        {
            // Keep the HDR attachment linear while retaining dev's display-space palette.
            output.color.rgb = srgbToLinear(meshletDebugColor(index - material.meshlets.x));
        }
        vertices[vertex] = output;
    }
    for (uint triangle = lane; triangle < meshlet.geometry.w; triangle += 32)
    {
        uint packed = meshletTriangles[meshlet.geometry.y + triangle];
        triangles[triangle] = uint3(packed & 255u, (packed >> 8) & 255u, (packed >> 16) & 255u);
    }
}
