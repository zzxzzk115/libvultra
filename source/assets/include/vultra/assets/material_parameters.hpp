#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/api_annotations.hpp>

namespace vultra
{
    // Numeric OpenPBR parameters. Texture bindings and raster sidedness stay with the imported model.
    struct VULTRA_REFLECT VULTRA_BIND_POD MaterialParameters
    {
        VULTRA_PROPERTY("label=Base red;min=0;max=4")
        float baseRed = 1;
        VULTRA_PROPERTY("label=Base green;min=0;max=4")
        float baseGreen = 1;
        VULTRA_PROPERTY("label=Base blue;min=0;max=4")
        float baseBlue = 1;
        VULTRA_PROPERTY("label=Base alpha;min=0;max=1")
        float baseAlpha = 1;
        VULTRA_PROPERTY("label=Base weight;min=0;max=1")
        float baseWeight = 1;
        VULTRA_PROPERTY("label=Metalness;min=0;max=1")
        float baseMetalness = 0;
        VULTRA_PROPERTY("label=Diffuse roughness;min=0;max=1")
        float baseDiffuseRoughness = 0;
        VULTRA_PROPERTY("label=Specular weight;min=0;max=1")
        float specularWeight = 1;
        VULTRA_PROPERTY("label=Specular red;min=0;max=4")
        float specularRed = 1;
        VULTRA_PROPERTY("label=Specular green;min=0;max=4")
        float specularGreen = 1;
        VULTRA_PROPERTY("label=Specular blue;min=0;max=4")
        float specularBlue = 1;
        VULTRA_PROPERTY("label=Roughness;min=0;max=1")
        float specularRoughness = 0.3f;
        VULTRA_PROPERTY("label=Specular IOR;min=1;max=3")
        float specularIor = 1.5f;
        VULTRA_PROPERTY("label=Coat weight;min=0;max=1")
        float coatWeight = 0;
        VULTRA_PROPERTY("label=Coat roughness;min=0;max=1")
        float coatRoughness = 0.1f;
        VULTRA_PROPERTY("label=Coat IOR;min=1;max=3")
        float coatIor = 1.5f;
        VULTRA_PROPERTY("label=Emission red;min=0;max=4")
        float emissionRed = 0;
        VULTRA_PROPERTY("label=Emission green;min=0;max=4")
        float emissionGreen = 0;
        VULTRA_PROPERTY("label=Emission blue;min=0;max=4")
        float emissionBlue = 0;
        VULTRA_PROPERTY("label=Emission luminance;min=0;max=100")
        float emissionLuminance = 1;
        VULTRA_PROPERTY("label=Normal scale;min=0;max=4")
        float normalScale = 1;
        VULTRA_PROPERTY("label=Occlusion strength;min=0;max=1")
        float occlusionStrength = 1;
        VULTRA_PROPERTY("label=Alpha cutoff;min=-1;max=1")
        float alphaCutoff = -1;

        static MaterialParameters fromMaterial(const SurfaceMaterial& material);
        void                      applyTo(SurfaceMaterial& material) const;
        void                      validate() const;
        friend bool               operator==(const MaterialParameters&, const MaterialParameters&) = default;
    };
} // namespace vultra
