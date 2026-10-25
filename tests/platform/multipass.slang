// VRI clip Y points up, depth is [0, 1], and texture UV starts at the top left.
struct Parameters
{
    float4 transform; // Rotation, horizontal offset, distance, scale.
    float4 tint;
    float4 viewport; // Aspect ratio, post gain, unused, unused.
};

[[vk::binding(0, 0)]] Texture2D<float4>          source;
[[vk::binding(1, 0)]] SamplerState               sourceSampler;
[[vk::binding(2, 0)]] ConstantBuffer<Parameters> parameters;

struct MeshVertex
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct SceneVertex
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

float3 rotate(float3 p, float angle)
{
    float3 tilted = float3(p.x, p.y * 0.9393727 - p.z * 0.3428978, p.y * 0.3428978 + p.z * 0.9393727);
    return float3(tilted.x * cos(angle) + tilted.z * sin(angle),
                  tilted.y,
                  -tilted.x * sin(angle) + tilted.z * cos(angle));
}

[shader("vertex")] SceneVertex sceneVertex(MeshVertex input)
{
    float3 world = rotate(input.position, parameters.transform.x) * parameters.transform.w;
    world += float3(parameters.transform.y, 0, parameters.transform.z);
    SceneVertex result;
    // Perspective, near 0.1 / far 10. No backend-specific UV or depth compensation.
    result.position =
        float4(world.x * 2 / parameters.viewport.x, world.y * 2, (10.0 / 9.9) * world.z - (1.0 / 9.9), world.z);
    result.normal = rotate(input.normal, parameters.transform.x);
    result.uv     = input.uv;
    return result;
}

    [shader("fragment")] float4 sceneFragment(SceneVertex input) :
    SV_Target0
{
    float light = 0.3 + 0.7 * max(dot(normalize(input.normal), normalize(float3(-0.4, 0.6, -1))), 0);
    return float4(source.SampleLevel(sourceSampler, input.uv, 0).rgb * parameters.tint.rgb * light, 1);
}

struct PostVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")] PostVertex postVertex(uint index : SV_VertexID)
{
    PostVertex result;
    result.uv       = float2((index << 1) & 2, index & 2);
    result.position = float4(result.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}

    [shader("fragment")] float4 postFragment(PostVertex input) :
    SV_Target0
{
    float3 color = source.SampleLevel(sourceSampler, input.uv, 0).rgb;
    return float4(color * parameters.viewport.y + float3(input.uv, 0.25) * 0.05, 1);
}
