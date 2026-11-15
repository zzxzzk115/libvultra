// `import vultra.material;` gives a stage the kernel's surface and lighting code: the surface a scene material
// gives a rasterized vertex, and the OpenPBR response of that surface to directional, scene and environment
// light. Forward shading uses the same code.
//
// It declares the OpenPBR tables at set 0, binding 11, and the bound scene material: its textures and samplers
// in set 1 and its per-draw constants as push constants, as SceneMaterials binds them.

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
