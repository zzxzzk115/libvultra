// `import vultra.gbuffer;` gives a stage the encodings of the G-buffer layers forward shading writes, so a stage
// that writes or reads them uses the same ones. There are two profiles, and the kernel's gbufferLayers() lists
// their formats:
//
// - full (SPIR-V and the other desktop targets): scene.normal octahedral rg16f, scene.albedo rgba8 sRGB (base color
//   times base weight, occlusion in alpha), scene.material rgba16 (roughness, metalness, specular weight, diffuse
//   roughness), scene.emission rgba16f, scene.motion rg16f, scene.coat rgba16f (weight, roughness, IOR) and
//   scene.specular rgba16f (color, IOR). Nothing is quantized below its source.
// - compact (WGSL, defined by VULTRA_GBUFFER_COMPACT): the layers fit WebGPU's default 32 bytes of color
//   attachments per sample together with scene.hdr. scene.material is rg8 roughness and metalness, scene.emission
//   is RGB9E5 in an r32ui, and there is no scene.coat or scene.specular: those take OpenPBR's defaults.
//
// Stages declare these layers with the format "standard" and use the layer types below, so one source compiles
// for both profiles.

#ifdef VULTRA_GBUFFER_COMPACT
typealias VultraMaterialLayer = float2;
typealias VultraEmissionLayer = uint;
#else
typealias VultraMaterialLayer = float4;
typealias VultraEmissionLayer = float4;
#endif

// A unit normal as a point of the octahedron unfolded onto [-1, 1]^2.
float2 vultraEncodeNormal(float3 normal)
{
    const float3 projected = normal / (abs(normal.x) + abs(normal.y) + abs(normal.z));
    if (projected.z >= 0)
    {
        return projected.xy;
    }
    const float2 signs = float2(projected.x >= 0 ? 1 : -1, projected.y >= 0 ? 1 : -1);
    return (1 - abs(projected.yx)) * signs;
}

float3 vultraDecodeNormal(float2 encoded)
{
    float3      normal = float3(encoded, 1 - abs(encoded.x) - abs(encoded.y));
    const float fold   = saturate(-normal.z);
    normal.x += normal.x >= 0 ? -fold : fold;
    normal.y += normal.y >= 0 ? -fold : fold;
    return normalize(normal);
}

VultraMaterialLayer vultraEncodeMaterial(float roughness, float metalness, float specularWeight, float diffuseRoughness)
{
#ifdef VULTRA_GBUFFER_COMPACT
    return float2(roughness, metalness);
#else
    return float4(roughness, metalness, specularWeight, diffuseRoughness);
#endif
}

// Roughness, metalness, specular weight and diffuse roughness; the compact profile has OpenPBR's defaults for the
// last two.
float4 vultraDecodeMaterial(VultraMaterialLayer layer)
{
#ifdef VULTRA_GBUFFER_COMPACT
    return float4(layer, 1, 0);
#else
    return layer;
#endif
}

#ifdef VULTRA_GBUFFER_COMPACT
// Non-negative radiance as RGB9E5: three 9-bit mantissas and a shared 5-bit exponent, as Vulkan's
// E5B9G9R9_UFLOAT_PACK32 lays them out. Channels below 1/512 of the largest one round to zero.
VultraEmissionLayer vultraEncodeEmission(float3 radiance)
{
    const float  largestValue = 65408.0; // (511 / 512) * 2^15.
    const float3 clamped      = clamp(radiance, 0, largestValue);
    const float  largest      = max(clamped.r, max(clamped.g, clamped.b));
    int          exponent     = int(max(-16.0, floor(log2(largest)))) + 16;
    float        step         = exp2(float(exponent - 24));
    if (uint(floor(largest / step + 0.5)) == 512)
    {
        step *= 2;
        exponent += 1;
    }
    const uint3 mantissa = uint3(floor(clamped / step + 0.5));
    return mantissa.r | (mantissa.g << 9) | (mantissa.b << 18) | (uint(exponent) << 27);
}

float3 vultraDecodeEmission(VultraEmissionLayer encoded)
{
    const uint3 mantissa = uint3(encoded & 0x1ffu, (encoded >> 9) & 0x1ffu, (encoded >> 18) & 0x1ffu);
    return float3(mantissa) * exp2(float(int(encoded >> 27) - 24));
}
#else
VultraEmissionLayer vultraEncodeEmission(float3 radiance)
{
    return float4(radiance, 0);
}

float3 vultraDecodeEmission(VultraEmissionLayer encoded)
{
    return encoded.rgb;
}

// scene.coat: weight, roughness and IOR of the coat.
float4 vultraEncodeCoat(float weight, float roughness, float ior)
{
    return float4(weight, roughness, ior, 0);
}

// scene.specular: specular color and IOR.
float4 vultraEncodeSpecular(float3 color, float ior)
{
    return float4(color, ior);
}
#endif
