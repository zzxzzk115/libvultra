// `import vultra.material;` gives a stage the kernel's surface and lighting code: the surface a scene material
// gives a rasterized vertex, the OpenPBR response of that surface to directional, scene and environment light,
// and IVultraMaterial, the interface of a material type. Forward shading uses the same code.
//
// It declares the OpenPBR tables at set 0, binding 11, and the bound scene material: its textures, samplers and
// the parameter blocks in set 1 and the draw's indices as push constants, as SceneMaterials binds them.

#include "lib/ibl.slangh"
#include "lib/light.slangh"
#include "lib/surface_data.slangh"

typedef OpenPBR_PreparedBsdf VultraBsdf;

OpenPbrSurface vultraOpenPbrSurface(VultraSurface surface)
{
    OpenPbrSurface result;
    result.baseColor            = surface.baseColor;
    result.baseWeight           = surface.baseWeight;
    result.baseMetalness        = surface.metallic;
    result.specularRoughness    = surface.roughness;
    result.baseDiffuseRoughness = surface.baseDiffuseRoughness;
    result.specularWeight       = surface.specularWeight;
    result.specularIor          = surface.specularIor;
    result.specularColor        = surface.specularColor;
    result.coatWeight           = surface.coatWeight;
    result.coatRoughness        = max(surface.coatRoughness, 0.03);
    result.coatIor              = surface.coatIor;
    return result;
}

// The BSDF of a surface seen from the unit direction `toView`, for the light functions of this module.
VultraBsdf vultraPrepareBsdf(VultraSurface surface, float3 toView)
{
    return prepareOpenPbr(vultraOpenPbrSurface(surface), surface.normal, toView);
}

// Image-based light from prefiltered equirectangular maps: a split-sum approximation, separate from the
// OpenPBR evaluation of direct light. `maxMip` is the specular map's mip count minus one.
float3 vultraEnvironmentLight(VultraSurface     surface,
                              float3            toView,
                              Texture2D<float4> diffuseMap,
                              Texture2D<float4> specularMap,
                              Texture2D<float4> brdfLut,
                              SamplerState      environmentSampler,
                              float             maxMip)
{
    return evaluateIbl(vultraOpenPbrSurface(surface),
                       surface.normal,
                       toView,
                       diffuseMap,
                       specularMap,
                       brdfLut,
                       environmentSampler,
                       maxMip);
}

// The interpolated vertex of a rasterized surface and the side it is seen from.
struct VultraSurfaceInput
{
    SurfaceVertex vertex;
    bool          frontFace;
};

// A material type. A type is a struct with a member `parameters` whose fields are the type's parameters, as its
// registration lists them; the program's vultra.materials fills them from the material's parameter block.
// surface() runs where the scene is rasterized with the bound material's textures (set 1); shade() runs wherever a
// surface is lit, also in a pass that only reads the G-buffer, so it reads nothing but its parameters and arguments.
interface IVultraMaterial
{
    // The surface at a rasterized point: what the required G-buffer layers hold. A type that cuts out
    // fragments discards them here.
    VultraSurface surface(VultraSurfaceInput input);

    // Radiance reflected towards `toView` per unit radiance arriving from `toLight` (unit directions from the
    // surface): the BSDF times the incident cosine.
    float3 shade(VultraSurface surface, float3 toLight, float3 toView);
}

// vultra.openpbr, the type of the materials of model files: the OpenPBR subset the kernel implements. Its
// parameters are the block GpuScene writes for a SurfaceMaterial, read through vultraOpenPbrParameters().
struct VultraOpenPbr : IVultraMaterial
{
    VultraSurface surface(VultraSurfaceInput input)
    {
        const float3 baseColor = vultraBaseColor(input.vertex.uv, input.vertex.color).rgb;
        return vultraSampleSurface(input.vertex, input.frontFace, baseColor);
    }

    float3 shade(VultraSurface surface, float3 toLight, float3 toView)
    {
        return vultraDirectionalLight(vultraPrepareBsdf(surface, toView), toLight, 1);
    }
};
