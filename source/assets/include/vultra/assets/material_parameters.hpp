#pragma once

#include <vultra/assets/scene_data.hpp>

namespace vultra
{
    // Numeric OpenPBR parameters. Texture bindings and raster sidedness stay with the imported model.
    struct MaterialParameters
    {
        float baseRed = 1;
        float baseGreen = 1;
        float baseBlue = 1;
        float baseAlpha = 1;
        float baseWeight = 1;
        float baseMetalness = 0;
        float baseDiffuseRoughness = 0;
        float specularWeight = 1;
        float specularRed = 1;
        float specularGreen = 1;
        float specularBlue = 1;
        float specularRoughness = 0.3f;
        float specularIor = 1.5f;
        float coatWeight = 0;
        float coatRoughness = 0.1f;
        float coatIor = 1.5f;
        float emissionRed = 0;
        float emissionGreen = 0;
        float emissionBlue = 0;
        float emissionLuminance = 1;
        float normalScale = 1;
        float occlusionStrength = 1;
        float alphaCutoff = -1;

        static MaterialParameters fromMaterial(const SurfaceMaterial& material);
        void                      applyTo(SurfaceMaterial& material) const;
        void                      validate() const;
        friend bool               operator==(const MaterialParameters&, const MaterialParameters&) = default;
    };
} // namespace vultra
