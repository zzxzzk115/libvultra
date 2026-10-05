Shader "Examples/PaintedMetal"
{
    Properties
    {
        baseColor ("Base Color", Color) = (0.8, 0.2, 0.1, 1)
        roughness ("Roughness", Range(0.03, 1)) = 0.4
        metalness ("Metalness", Range(0, 1)) = 0.8
        [HDR] emission ("Emission", Color) = (0.1, 0.2, 0.3, 1)
        coat ("Coat", Range(0, 1)) = 0.5
        opacity ("Opacity", Range(0, 1)) = 1
        [Enum(Off, 0, Front, 1, Back, 2)] cull ("Cull", Integer) = 2
        [Normal] normalMap ("Normal", 2D) = "normal" {}
    }
    SubShader
    {
        Tags { "RenderPipeline" = "Vultra" "RenderType" = "AlphaTest" }
        Cull [cull]
        ZWrite On
        Surface
        {
            Model OpenPBR
            Entry surfaceMain
            SLANGPROGRAM
            void surfaceMain(SurfaceInput input, inout SurfaceOutput output)
            {
                output.baseColor = material.baseColor.rgb * input.color.rgb;
                output.specularRoughness = material.roughness;
                output.metalness = material.metalness;
                output.emission = material.emission.rgb;
                output.coatWeight = material.coat;
                output.opacity = material.opacity;
                output.tangentSpaceNormal = decodeSurfaceNormal(
                    material.normalMap.Sample(material.normalMapSampler, input.uv),
                    material.normalMapEncoding);
            }
            ENDSLANG
        }
    }
}
