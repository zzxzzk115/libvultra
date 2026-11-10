Shader "Tests/UnlitSurface"
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
            void surfaceMain(SurfaceInput input, inout UnlitSurfaceOutput output)
            {
                float4 texel = material.mask.Sample(material.maskSampler, input.uv);
                output.color = material.tint.rgb * input.color.rgb * texel.rgb;
                output.opacity = material.opacity * texel.a;
                output.alphaCutoff = material.cutoff;
            }
            ENDSLANG
        }
    }
}
