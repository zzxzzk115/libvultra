import vultra.gbuffer;
import vultra.material;

#include "resources/frame_block.slangh"
#include "lib/surface_lighting.slangh"

[shader("vertex")] SurfaceVertex vertexMain(MeshVertex input)
{
    return vultraTransformVertex(input, primitiveTransforms[material.instance.x], frame.viewProjection);
}

// The material's surface with the frame's roughness and metalness overrides; negative overrides keep it.
VultraSurface forwardSurface(SurfaceVertex input, bool frontFace, float3 baseColor)
{
    VultraSurface surface = vultraSampleSurface(input, frontFace, baseColor);
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
    float3 baseColor = vultraBaseColor(input.uv, input.color).rgb;
    if (material.meshlets.w != 0)
    {
        return float4(input.color.rgb, 1);
    }
    return float4(shadeSurface(forwardSurface(input, frontFace, baseColor)), 1);
}

// The G-buffer variant: the same shading into scene.hdr, plus the surface layers and motion.
struct GBufferVertex
{
    SurfaceVertex surface;
    float4        clip : TEXCOORD1;
    float4        previousClip : TEXCOORD2;
};

struct GBufferOutput
{
    float4 hdr : SV_Target0;
    float2 normal : SV_Target1;
    float4 albedo : SV_Target2;
    float2 material : SV_Target3;
    uint   emission : SV_Target4;
    float2 motion : SV_Target5;
};

[shader("vertex")] GBufferVertex vertexGBuffer(MeshVertex input)
{
    PrimitiveTransform instance = primitiveTransforms[material.instance.x];
    GBufferVertex      result;
    result.surface      = vultraTransformVertex(input, instance, frame.viewProjection);
    result.clip         = result.surface.position;
    result.previousClip = mul(camera.previousViewProjection, mul(instance.previousModel, float4(input.position, 1)));
    return result;
}

[shader("fragment")] GBufferOutput fragmentGBuffer(GBufferVertex input, bool frontFace : SV_IsFrontFace)
{
    float3        baseColor = vultraBaseColor(input.surface.uv, input.surface.color).rgb;
    VultraSurface surface   = forwardSurface(input.surface, frontFace, baseColor);
    GBufferOutput output;
    output.hdr      = float4(shadeSurface(surface), 1);
    output.normal   = vultraEncodeNormal(surface.normal);
    output.albedo   = float4(surface.baseColor, surface.occlusion);
    output.material = float2(surface.roughness, surface.metallic);
    output.emission = vultraEncodeEmission(surface.emission);
    float2 ndc      = input.clip.xy / input.clip.w;
    output.motion   = (input.previousClip.xy / input.previousClip.w - ndc) * float2(0.5, -0.5);
    return output;
}
