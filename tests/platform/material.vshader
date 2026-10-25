Shader "Tests/CookedUnlit"
{
    Properties
    {
        color ("Color", Color) = (0.2, 0.4, 0.6, 1)
        gain ("Gain", Float) = 1
        [NoScaleOffset] baseMap ("Base map", 2D) = "white" {}
    }
    Variant "Default" { Defines { INVERT = 0 } }
    Variant "Invert" { Defines { INVERT = 1 } }
    SubShader
    {
        Cull Off
        Pass
        {
            Name "Unlit"
            Vertex vertexMain
            Fragment fragmentMain
            SLANGPROGRAM
            struct Vertex
            {
                float4 position : SV_Position;
                float2 uv : TEXCOORD0;
            };
            Vertex vertexMain(uint index : SV_VertexID)
            {
                const float2 uv = float2((index << 1) & 2, index & 2);
                Vertex result;
                result.position = float4(uv * 2 - 1, 0.5, 1);
                result.uv = uv;
                return result;
            }
            float4 fragmentMain(Vertex vertex) : SV_Target0
            {
                float3 color = material.baseMap.Sample(material.baseMapSampler, vertex.uv).rgb *
                               material.color.rgb * material.gain;
                #if INVERT
                color = 1 - color;
                #endif
                return float4(color, 1);
            }
            ENDSLANG
        }
    }
}
