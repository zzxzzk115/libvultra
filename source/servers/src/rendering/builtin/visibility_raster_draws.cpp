#include <vultra/servers/rendering/builtin/visibility_raster_pipeline.hpp>
#include <vultra/servers/rendering/scene.hpp>

namespace vultra
{
    std::expected<std::vector<VisibilityRasterDraw>, std::string>
    visibilityRasterDraws(const GpuScene& scene, std::span<const uint32_t> visiblePrimitives)
    {
        if (!scene.indices || scene.materials.size() != scene.materialTextures.size())
            return std::unexpected("Invalid visibility raster scene");
        std::vector<VisibilityRasterDraw> draws;
        draws.reserve(visiblePrimitives.size());
        for (const auto index : visiblePrimitives)
        {
            if (index >= scene.primitives.size())
                return std::unexpected("Visibility primitive index is out of range");
            const auto& primitive = scene.primitives[index];
            if (primitive.material >= scene.materials.size() || primitive.indexCount % 3 != 0 ||
                uint64_t(primitive.firstIndex) + primitive.indexCount > scene.indices->desc.size / sizeof(uint32_t) ||
                !scene.materialTextures[primitive.material][2])
                return std::unexpected("Invalid visibility primitive geometry or material");
            const auto mirrored = scene.primitiveMirroredExpected(index);
            if (!mirrored)
                return std::unexpected(mirrored.error());
            const auto& material = scene.materials[primitive.material];
            float       normal   = 0;
            if (material.normalTexture.image >= 0)
                normal = scene.materialTextures[primitive.material][2]->isTwoChannel() ? 2.f : 1.f;
            VisibilityRasterDraw draw;
            draw.firstIndex         = primitive.firstIndex;
            draw.indexCount         = primitive.indexCount;
            draw.materialIndex      = primitive.material;
            draw.doubleSided        = material.doubleSided;
            draw.mirrored           = *mirrored;
            draw.alphaMasked        = material.alphaCutoff > 0;
            draw.material.baseColor = material.baseColor;
            draw.material.surface   = {material.baseMetalness,
                                       material.specularRoughness,
                                       material.specularIor,
                                       material.normalScale};
            draw.material.emission  = {material.emissionColor * material.emissionLuminance, material.occlusionStrength};
            draw.material.coat      = {material.coatWeight,
                                       material.coatRoughness,
                                       material.coatIor,
                                       material.specularWeight};
            draw.material.specular  = {material.specularColor, material.baseDiffuseRoughness};
            draw.material.flags     = {material.alphaCutoff, material.baseWeight, 0, normal};
            draw.material.instance.x = index;
            draws.push_back(draw);
        }
        return draws;
    }
} // namespace vultra
