Shader "Tests/PreparedScene"
{
    Properties
    {
        gain ("Gain", Float) = 1
        lod ("Mip", Float) = 0
    }
    SubShader
    {
        Cull Off
        Pass
        {
            Name "Scene"
            Vertex vertexMain
            Fragment fragmentMain
            SLANGPROGRAM
            struct DrawData { float4x4 transform; };
            [[vk::push_constant]] ConstantBuffer<DrawData> draw;
            Texture2D<float4> sceneTexture;
            SamplerState sceneSampler;
            struct VertexInput
            {
                float3 position : POSITION;
                float2 uv : TEXCOORD0;
            };
            struct Vertex
            {
                float4 position : SV_Position;
                float2 uv : TEXCOORD0;
            };
            Vertex vertexMain(VertexInput input)
            {
                Vertex result;
                result.position = mul(draw.transform, float4(input.position, 1));
                result.uv = input.uv;
                return result;
            }
            float4 fragmentMain(Vertex vertex) : SV_Target0
            {
                float3 color = sceneTexture.SampleLevel(sceneSampler, vertex.uv, material.lod).rgb;
                return float4(color * material.gain, 1);
            }
            ENDSLANG
        }
    }
}
