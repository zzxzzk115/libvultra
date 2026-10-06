#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/api_annotations.hpp>

namespace vultra
{
    // Numeric OpenPBR parameters. Texture bindings and raster sidedness stay with the imported model.
    struct VULTRA_REFLECT VULTRA_BIND_POD MaterialParameters
    {
        VULTRA_PROPERTY("label=Base red;min=0;max=4;json=/base_color/0")
        float baseRed = 1;
        VULTRA_PROPERTY("label=Base green;min=0;max=4;json=/base_color/1")
        float baseGreen = 1;
        VULTRA_PROPERTY("label=Base blue;min=0;max=4;json=/base_color/2")
        float baseBlue = 1;
        VULTRA_PROPERTY("label=Base alpha;min=0;max=1;json=/base_color/3")
        float baseAlpha = 1;
        VULTRA_PROPERTY("label=Base weight;min=0;max=1;json=/base_weight")
        float baseWeight = 1;
        VULTRA_PROPERTY("label=Metalness;min=0;max=1;json=/metalness")
        float baseMetalness = 0;
        VULTRA_PROPERTY("label=Diffuse roughness;min=0;max=1;json=/diffuse_roughness")
        float baseDiffuseRoughness = 0;
        VULTRA_PROPERTY("label=Specular weight;min=0;max=1;json=/specular_weight")
        float specularWeight = 1;
        VULTRA_PROPERTY("label=Specular red;min=0;max=4;json=/specular_color/0")
        float specularRed = 1;
        VULTRA_PROPERTY("label=Specular green;min=0;max=4;json=/specular_color/1")
        float specularGreen = 1;
        VULTRA_PROPERTY("label=Specular blue;min=0;max=4;json=/specular_color/2")
        float specularBlue = 1;
        VULTRA_PROPERTY("label=Roughness;min=0;max=1;json=/roughness")
        float specularRoughness = 0.3f;
        VULTRA_PROPERTY("label=Specular IOR;min=1;max=3;json=/specular_ior")
        float specularIor = 1.5f;
        VULTRA_PROPERTY("label=Coat weight;min=0;max=1;json=/coat_weight")
        float coatWeight = 0;
        VULTRA_PROPERTY("label=Coat roughness;min=0;max=1;json=/coat_roughness")
        float coatRoughness = 0.1f;
        VULTRA_PROPERTY("label=Coat IOR;min=1;max=3;json=/coat_ior")
        float coatIor = 1.5f;
        VULTRA_PROPERTY("label=Emission red;min=0;max=4;json=/emission_color/0")
        float emissionRed = 0;
        VULTRA_PROPERTY("label=Emission green;min=0;max=4;json=/emission_color/1")
        float emissionGreen = 0;
        VULTRA_PROPERTY("label=Emission blue;min=0;max=4;json=/emission_color/2")
        float emissionBlue = 0;
        VULTRA_PROPERTY("label=Emission luminance;min=0;max=100;json=/emission_luminance")
        float emissionLuminance = 1;
        VULTRA_PROPERTY("label=Normal scale;min=0;max=4;json=/normal_scale")
        float normalScale = 1;
        VULTRA_PROPERTY("label=Occlusion strength;min=0;max=1;json=/occlusion_strength")
        float occlusionStrength = 1;
        VULTRA_PROPERTY("label=Alpha cutoff;min=-1;max=1;json=/alpha_cutoff")
        float alphaCutoff = -1;

        static MaterialParameters fromMaterial(const SurfaceMaterial& material);
        void                      applyTo(SurfaceMaterial& material) const;
        void                      validate() const;
        friend bool               operator==(const MaterialParameters&, const MaterialParameters&) = default;
    };
} // namespace vultra
