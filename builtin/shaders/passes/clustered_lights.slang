// Conservative view-space clustered light masks. Bit i represents scene light i;
// the fixed 64-bit mask has no per-cluster list overflow.
struct ClusterConstants
{
    column_major float4x4 inverseProjection;
    float4 viewportClip; // width, height, near, far
    uint4 grid;          // tile columns, tile rows, depth slices, light count
    float4 lightBounds[64]; // view-space center and conservative radius; negative radius means directional
};
[[vk::binding(0, 0)]] ConstantBuffer<ClusterConstants> constants;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint2> clusters;

float3 viewPoint(float2 ndc, float positiveDepth)
{
    float4 a = mul(constants.inverseProjection, float4(ndc, 0, 1));
    float4 b = mul(constants.inverseProjection, float4(ndc, 1, 1));
    float3 nearPoint = a.xyz / a.w;
    float3 farPoint = b.xyz / b.w;
    float t = (-positiveDepth - nearPoint.z) / (farPoint.z - nearPoint.z);
    return lerp(nearPoint, farPoint, t);
}

[shader("compute")]
[numthreads(4, 4, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= constants.grid.x || id.y >= constants.grid.y || id.z >= constants.grid.z)
        return;
    float2 viewport = constants.viewportClip.xy;
    float2 lowerPixel = float2(id.xy * 16);
    float2 upperPixel = min(float2((id.xy + 1) * 16), viewport);
    float2 lowerNdc = lowerPixel / viewport * float2(2, -2) + float2(-1, 1);
    float2 upperNdc = upperPixel / viewport * float2(2, -2) + float2(-1, 1);
    float nearPlane = constants.viewportClip.z;
    float farPlane = constants.viewportClip.w;
    float ratio = farPlane / nearPlane;
    // Expand slightly to avoid excluding boundary pixels due to floating-point rounding.
    float nearDepth = nearPlane * pow(ratio, float(id.z) / float(constants.grid.z)) * (1 - 1e-5);
    float farDepth = nearPlane * pow(ratio, float(id.z + 1) / float(constants.grid.z)) * (1 + 1e-5);
    float3 lower = float3(1e30, 1e30, 1e30);
    float3 upper = -lower;
    for (uint corner = 0; corner < 8; ++corner)
    {
        float2 ndc = float2((corner & 1) != 0 ? upperNdc.x : lowerNdc.x,
                            (corner & 2) != 0 ? upperNdc.y : lowerNdc.y);
        float3 p = viewPoint(ndc, (corner & 4) != 0 ? farDepth : nearDepth);
        lower = min(lower, p);
        upper = max(upper, p);
    }
    float3 margin = max(max(abs(lower), abs(upper)) * 1e-5, 1e-4);
    lower -= margin;
    upper += margin;
    uint2 mask = uint2(0, 0);
    for (uint i = 0; i < constants.grid.w; ++i)
    {
        float4 bound = constants.lightBounds[i];
        float3 delta = max(max(lower - bound.xyz, bound.xyz - upper), 0);
        bool intersects = bound.w < 0 || dot(delta, delta) <= bound.w * bound.w;
        if (intersects)
            mask[i >> 5] |= 1u << (i & 31);
    }
    clusters[(id.z * constants.grid.y + id.y) * constants.grid.x + id.x] = mask;
}
