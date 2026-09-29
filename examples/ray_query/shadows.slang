struct Parameters
{
    column_major float4x4 viewProjection;
    float4 light;
    float4 options;
};
[[vk::push_constant]] Parameters parameters;
[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene;

struct VertexInput
{
    float4 position : POSITION;
    float4 normal : NORMAL;
    float4 color : COLOR0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : NORMAL;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(parameters.viewProjection, input.position);
    output.worldPosition = input.position.xyz;
    output.normal = input.normal.xyz;
    output.color = input.color.rgb;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float3 toLight = parameters.light.xyz - input.worldPosition;
    float distance = length(toLight);
    float3 direction = toLight / distance;
    float visibility = 1;
    if (parameters.options.y > 0.5)
    {
        RayDesc ray;
        ray.Origin = input.worldPosition + normal * 0.001;
        ray.Direction = direction;
        ray.TMin = 0.001;
        ray.TMax = distance - 0.002;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(scene, RAY_FLAG_NONE, 255, ray);
        while (query.Proceed())
        {
        }
        visibility = query.CommittedStatus() == COMMITTED_NOTHING ? 1 : 0;
    }
    return float4(input.color * max(parameters.options.x, max(dot(normal, direction), 0) * visibility), 1);
}
