// Isolates depth testing from material and lighting shaders.
struct Input
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 tangent : TANGENT;
};

struct Output
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;
    float4 tangent : TEXCOORD3;
};

[shader("vertex")]
Output vertexMain(Input input)
{
    Output output;
    output.position = float4(input.position, 1);
    output.normal = input.normal;
    output.uv = input.uv;
    output.color = input.color;
    output.tangent = input.tangent;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Output input) : SV_Target
{
    float red = input.color.x + input.normal.x + input.uv.x + input.tangent.x;
    return float4(red, 0, 0, 0.5);
}
