#include <vultra/assets/material_parameters.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace vultra
{
    MaterialParameters MaterialParameters::fromMaterial(const SurfaceMaterial& material)
    {
        return {material.baseColor.r,          material.baseColor.g,
                material.baseColor.b,          material.baseColor.a,
                material.baseWeight,           material.baseMetalness,
                material.baseDiffuseRoughness, material.specularWeight,
                material.specularColor.r,      material.specularColor.g,
                material.specularColor.b,      material.specularRoughness,
                material.specularIor,          material.coatWeight,
                material.coatRoughness,        material.coatIor,
                material.emissionColor.r,      material.emissionColor.g,
                material.emissionColor.b,      material.emissionLuminance,
                material.normalScale,          material.occlusionStrength,
                material.alphaCutoff};
    }

    void MaterialParameters::applyTo(SurfaceMaterial& material) const
    {
        material.baseColor            = {baseRed, baseGreen, baseBlue, baseAlpha};
        material.baseWeight           = baseWeight;
        material.baseMetalness        = baseMetalness;
        material.baseDiffuseRoughness = baseDiffuseRoughness;
        material.specularWeight       = specularWeight;
        material.specularColor        = {specularRed, specularGreen, specularBlue};
        material.specularRoughness    = specularRoughness;
        material.specularIor          = specularIor;
        material.coatWeight           = coatWeight;
        material.coatRoughness        = coatRoughness;
        material.coatIor              = coatIor;
        material.emissionColor        = {emissionRed, emissionGreen, emissionBlue};
        material.emissionLuminance    = emissionLuminance;
        material.normalScale          = normalScale;
        material.occlusionStrength    = occlusionStrength;
        material.alphaCutoff          = alphaCutoff;
    }

    void MaterialParameters::validate() const
    {
        const std::array values {baseRed,
                                 baseGreen,
                                 baseBlue,
                                 baseAlpha,
                                 baseWeight,
                                 baseMetalness,
                                 baseDiffuseRoughness,
                                 specularWeight,
                                 specularRed,
                                 specularGreen,
                                 specularBlue,
                                 specularRoughness,
                                 specularIor,
                                 coatWeight,
                                 coatRoughness,
                                 coatIor,
                                 emissionRed,
                                 emissionGreen,
                                 emissionBlue,
                                 emissionLuminance,
                                 normalScale,
                                 occlusionStrength,
                                 alphaCutoff};
        if (std::ranges::any_of(values,
                                [](float value)
                                {
                                    return !std::isfinite(value);
                                }))
        {
            throw std::invalid_argument("Material parameters must be finite");
        }
        const std::array unitValues {baseAlpha,
                                     baseWeight,
                                     baseMetalness,
                                     baseDiffuseRoughness,
                                     specularWeight,
                                     specularRoughness,
                                     coatWeight,
                                     coatRoughness,
                                     occlusionStrength};
        if (std::ranges::any_of(unitValues,
                                [](float value)
                                {
                                    return value < 0 || value > 1;
                                }) ||
            baseRed < 0 || baseGreen < 0 || baseBlue < 0 || specularRed < 0 || specularGreen < 0 || specularBlue < 0 ||
            emissionRed < 0 || emissionGreen < 0 || emissionBlue < 0 || emissionLuminance < 0 || normalScale < 0 ||
            specularIor < 1 || coatIor < 1 || alphaCutoff < -1 || alphaCutoff > 1)
        {
            throw std::invalid_argument("Material weights/roughness/alpha must be in [0,1], colors/scales nonnegative, "
                                        "IOR at least 1 and alpha cutoff in [-1,1]");
        }
    }
} // namespace vultra
