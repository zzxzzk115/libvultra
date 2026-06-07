#ifndef VULTRA_SHADING_MODEL_ABI_GLSL
#define VULTRA_SHADING_MODEL_ABI_GLSL

// Path-agnostic shading-model / BXDF ABI.
//
// A custom shading model contributes a BXDF function with this signature:
//
//     vec3 <bxdfFunction>(VultraSurface s, VultraLight l, VultraShadingExtra e);
//
// returning the outgoing radiance contribution for a single light (i.e. the BRDF
// response already multiplied by N.L and the light's incoming radiance). The same
// function is called from any lighting path: the deferred fullscreen shader builds
// VultraSurface from the GBuffer; a forward+ material shader builds it from the
// material evaluation. This keeps custom BXDFs independent of the render path.
//
// The engine generates a dispatch:
//
//     vec3 vultra_eval_bxdf(uint model, VultraSurface s, VultraLight l);
//
// which switches over the registered model codes and calls the matching function.
// Per-model (not per-instance) extra params are fetched via vultra_shading_extra().

// Decoded surface, independent of how it was produced (GBuffer vs material eval).
struct VultraSurface
{
    vec3  albedo;     // base color (linear)
    vec3  normalWS;   // world-space shading normal (unit)
    vec3  viewDirWS;  // world-space direction toward the camera (unit)
    vec3  positionWS; // world-space position
    vec3  f0;         // specular reflectance at normal incidence
    float metallic;
    float roughness;  // perceptual roughness
    float ao;         // ambient occlusion (incl. SSAO)
    uint  shadingModel;
};

// One light sampled at the surface.
struct VultraLight
{
    vec3 L;        // unit direction from the surface toward the light
    vec3 radiance; // incoming radiance = color * intensity * attenuation * visibility
};

// Per-model extra params (a fixed-size block fetched from ShadingModelParamsBuffer
// by model code). The consuming shader provides the storage binding + accessor;
// when no extra-params buffer is bound the fields read as zero.
struct VultraShadingExtra
{
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec4 v3;
};

#endif // VULTRA_SHADING_MODEL_ABI_GLSL
