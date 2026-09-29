#include "lib/depth.slangh"
#include "lib/ibl.slangh"
#include "lib/light.slangh"
#include "lib/shadow.slangh"
#include "resources/frame_block.slangh"
#include "resources/gpu_material.slangh"

float3 shadeSurface(float2 uv,
                    float3 baseColor,
                    float3 emission,
                    float  metallic,
                    float  roughness,
                    float3 normal,
                    float3 view,
                    float  visibility,
                    float  occlusion)
{
    DirectionalLight light;
    light.directionToLight = frame.lightDirection.xyz;
    light.radiance         = frame.lightColor.rgb * frame.lightDirection.w;
    OpenPbrSurface surface;
    surface.baseColor            = baseColor;
    surface.baseWeight           = material.flags.y;
    surface.baseMetalness        = metallic;
    surface.specularRoughness    = roughness;
    surface.baseDiffuseRoughness = material.specular.w;
    surface.specularWeight       = material.coat.w * specularTexture.Sample(materialSampler, uv).a;
    surface.specularIor          = material.surface.z;
    surface.specularColor        = material.specular.rgb * specularColorTexture.Sample(materialSampler, uv).rgb;
    surface.coatWeight           = material.coat.x;
    surface.coatRoughness        = max(material.coat.y, 0.03);
    surface.coatIor              = material.coat.z;
    OpenPBR_PreparedBsdf bsdf    = prepareOpenPbr(surface, normal, view);
    // glTF emission is radiance, independent of the perturbed shading normal and coat lobes.
    float3 result = evaluateDirectionalLight(bsdf, light, visibility) + emission;
    if (frame.options.x > 0.5)
    {
        float3 ibl =
            evaluateIbl(surface, normal, view, diffuseMap, specularMap, brdfLut, environmentSampler, frame.options.w);
        result += ibl * frame.lightColor.w * occlusion;
    }
    return result;
}

SurfaceVertex transformSurfaceVertex(MeshVertex input)
{
    SurfaceVertex result;
    result.position      = mul(frame.viewProjection, float4(input.position, 1));
    result.worldPosition = input.position;
    result.normal        = input.normal;
    result.uv            = input.uv;
    result.color         = input.color;
    result.tangent       = input.tangent;
    return result;
}

[shader("vertex")] SurfaceVertex vertexMain(MeshVertex input)
{
    return transformSurfaceVertex(input);
}

[shader("fragment")] float4 fragmentMain(SurfaceVertex input) :
    SV_Target
{
    float3 baseColor = readBaseColor(input.uv, input.color).rgb;
    if (material.meshlets.w != 0)
    {
        return float4(input.color.rgb, 1);
    }
    float3 view      = normalize(frame.cameraPosition.xyz - input.worldPosition);
    float3 normal    = surfaceNormal(input, view);
    float2 mr        = metallicRoughnessTexture.Sample(materialSampler, input.uv).bg;
    float  metallic  = saturate(material.surface.x * mr.x);
    float  roughness = clamp(material.surface.y * mr.y, 0.03, 1);
    if (frame.overrides.x >= 0)
    {
        roughness = clamp(frame.overrides.x, 0.03, 1);
    }
    if (frame.overrides.y >= 0)
    {
        metallic = saturate(frame.overrides.y);
    }
    float  occlusion   = lerp(1, occlusionTexture.Sample(materialSampler, input.uv).r, material.emission.w);
    float  cameraDepth = viewDepth(input.worldPosition, frame.view);
    float  visibility  = shadowVisibility(input.worldPosition, normalize(input.normal), cameraDepth);
    float3 emission    = emissionTexture.Sample(materialSampler, input.uv).rgb * material.emission.rgb;
    float3 result      = shadeSurface(input.uv, baseColor, emission, metallic, roughness, normal, view, visibility, occlusion);
    uint   debug       = uint(frame.cameraClip.w);
    if (debug == 1)
    {
        result = baseColor;
    }
    else if (debug == 2)
    {
        result = normal * 0.5 + 0.5;
    }
    else if (debug == 3)
    {
        const float3 colors[4] = {float3(1, 0.2, 0.1), float3(0.2, 1, 0.2), float3(0.2, 0.4, 1), float3(1, 0.2, 1)};
        result                 = colors[shadowCascade(cameraDepth)] * (0.25 + 0.75 * visibility);
    }
    else if (debug == 4)
    {
        result = visibility.xxx;
    }
    else if (debug == 5)
    {
        result = emission;
    }
    return float4(max(result, 0), 1);
}
