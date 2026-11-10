[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint4> results;

[shader("compute")]
[numthreads(2, 1, 1)]
void traceMain(uint3 id : SV_DispatchThreadID)
{
    RayDesc ray;
    ray.Origin = id.x == 0 ? float3(0, 0, 0) : float3(0, 0, -4);
    ray.Direction = id.x == 0 ? float3(0, 0, -1) : float3(0, 0, 1);
    ray.TMin = 0.01;
    ray.TMax = 10;
    RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 255, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    results[id.x] = hit ? uint4(1, query.CommittedInstanceID(), asuint(query.CommittedRayT()), 0) : uint4(0);
}
