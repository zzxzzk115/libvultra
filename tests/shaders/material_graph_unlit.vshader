Shader "Tests/GraphUnlit"
{
    Properties
    {
        [HDR] tint ("Radiance", Color) = (0.25, 0.5, 4, 1)
        opacity ("Opacity", Range(0, 1)) = 1
        cutoff ("Cutoff", Range(0, 1)) = 0.5
        mask ("Mask", 2D) = "white" {}
    }
    SubShader
    {
        Tags { "RenderPipeline" = "Vultra" "RenderType" = "AlphaTest" }
        Cull Off
        Surface
        {
            Model Unlit
            Entry surfaceMain
            SLANGPROGRAM
            #include "material_graph.slangh"
            void surfaceMain(SurfaceInput input, inout UnlitSurfaceOutput output)
            {
                float4 value = materialPattern(material.tint, 2, input.uv, material.mask, material.maskSampler);
                output.color = value.rgb * input.color.rgb;
                output.opacity = material.opacity * value.a;
                output.alphaCutoff = material.cutoff;
            }
            ENDSLANG
        }
    }
}
