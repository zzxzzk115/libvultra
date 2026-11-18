import vultra.gbuffer;
import vultra.material;
import vultra.materials;

#include "resources/frame_block.slangh"
#include "lib/surface_lighting.slangh"

[shader("vertex")] SurfaceVertex vertexMain(MeshVertex input)
{
    return vultraTransformVertex(input, instances[material.instance.x], frame.viewProjection);
}

// The bound material's surface with the frame's roughness and metalness overrides; negative overrides keep it.
VultraSurface forwardSurface(SurfaceVertex input, bool frontFace)
{
    VultraSurfaceInput surfaceInput;
    surfaceInput.vertex    = input;
    surfaceInput.frontFace = frontFace;
    VultraSurface surface  = vultraMaterialSurface(surfaceInput);
    if (frame.overrides.x >= 0)
    {
        surface.roughness = clamp(frame.overrides.x, 0.03, 1);
    }
    if (frame.overrides.y >= 0)
    {
        surface.metallic = saturate(frame.overrides.y);
    }
    return surface;
}

[shader("fragment")] float4 fragmentMain(SurfaceVertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    return float4(shadeSurface(forwardSurface(input, frontFace)), 1);
}

// The G-buffer variant: the same shading into scene.hdr, plus the surface layers and motion.
struct GBufferVertex
{
    SurfaceVertex surface;
    float4        clip : TEXCOORD1;
    float4        previousClip : TEXCOORD2;
};

// In the order of the kernel's gbufferLayers(), after scene.hdr.
struct GBufferOutput
{
    float4              hdr : SV_Target0;
    float2              normal : SV_Target1;
    float4              albedo : SV_Target2;
    VultraMaterialLayer material : SV_Target3;
    VultraEmissionLayer emission : SV_Target4;
    float2              motion : SV_Target5;
#ifndef VULTRA_GBUFFER_COMPACT
    float4 coat : SV_Target6;
    float4 specular : SV_Target7;
#endif
};

[shader("vertex")] GBufferVertex vertexGBuffer(MeshVertex input)
{
    InstanceData instance = instances[material.instance.x];
    GBufferVertex      result;
    result.surface      = vultraTransformVertex(input, instance, frame.viewProjection);
    result.clip         = result.surface.position;
    result.previousClip = mul(camera.previousViewProjection, mul(instance.previousModel, float4(input.position, 1)));
    return result;
}

[shader("fragment")] GBufferOutput fragmentGBuffer(GBufferVertex input, bool frontFace : SV_IsFrontFace)
{
    VultraSurface surface = forwardSurface(input.surface, frontFace);
    GBufferOutput output;
    output.hdr      = float4(shadeSurface(surface), 1);
    output.normal   = vultraEncodeNormal(surface.normal);
    // OpenPBR scales the base color by the base weight, so the layer keeps the product.
    output.albedo   = float4(surface.baseColor * surface.baseWeight, surface.occlusion);
    output.material = vultraEncodeMaterial(
        surface.roughness, surface.metallic, surface.specularWeight, surface.baseDiffuseRoughness);
    output.emission = vultraEncodeEmission(surface.emission);
#ifndef VULTRA_GBUFFER_COMPACT
    output.coat     = vultraEncodeCoat(surface.coatWeight, surface.coatRoughness, surface.coatIor);
    output.specular = vultraEncodeSpecular(surface.specularColor, surface.specularIor);
#endif
    // Where the surface is without this frame's jitter, which the previous frame's projection does not have.
    float2 ndc      = input.clip.xy / input.clip.w - cameraJitterNdc(camera);
    output.motion   = (input.previousClip.xy / input.previousClip.w - ndc) * float2(0.5, -0.5);
    return output;
}
