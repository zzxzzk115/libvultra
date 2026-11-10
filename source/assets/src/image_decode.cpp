#include "image_decode.hpp"

// Keep the full asset decoder private; Image I/O has its own PNG-only stb implementation.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <limits>
#include <stdexcept>

namespace vultra
{
    std::expected<SceneImage, std::string> asset_detail::decodeImageExpected(std::span<const std::byte> bytes)
    {
        if (bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) == 0)
        {
            return retainDdsExpected(bytes);
        }
        if (bytes.empty() || bytes.size() > size_t(std::numeric_limits<int>::max()))
        {
            return std::unexpected("Invalid encoded image size");
        }
        const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
        const int   size = int(bytes.size());
        if (stbi_is_16_bit_from_memory(data, size) || stbi_is_hdr_from_memory(data, size))
        {
            return std::unexpected("Material images require 8-bit channels; use Environment for HDR images");
        }
        int                                                        width    = 0;
        int                                                        height   = 0;
        int                                                        channels = 0;
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(data, size, &width, &height, &channels, 4),
            &stbi_image_free);
        if (!pixels)
        {
            return std::unexpected(std::string("Decode image: ") + stbi_failure_reason());
        }
        return SceneImage {uint32_t(width),
                           uint32_t(height),
                           {pixels.get(), pixels.get() + size_t(width) * height * 4},
                           {}};
    }

    SceneImage asset_detail::decodeImage(std::span<const std::byte> bytes)
    {
        auto image = decodeImageExpected(bytes);
        if (!image)
        {
            throw std::runtime_error(image.error());
        }
        return std::move(*image);
    }

    std::expected<SceneImage, std::string> loadSceneImageExpected(const std::filesystem::path&  path,
                                                                  const ExpectedSourceObserver& observer,
                                                                  const AssetSource*            source)
    {
        const auto bytes = readSourceFileExpected(path, observer, source);
        if (!bytes)
        {
            return std::unexpected("Image " + path.string() + ": " + bytes.error());
        }
        auto image = asset_detail::decodeImageExpected(*bytes);
        if (!image)
        {
            return std::unexpected("Image " + path.string() + ": " + image.error());
        }
        return image;
    }

    SceneImage
    loadSceneImage(const std::filesystem::path& path, const SourceObserver& observer, const AssetSource* source)
    {
        try
        {
            return asset_detail::decodeImage(readSourceFile(path, observer, source));
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("Image " + path.string() + ": " + error.what());
        }
    }

    std::expected<bool, std::string> asset_detail::imageHasOpaqueAlphaExpected(const SceneImage& image)
    {
        if (!image.dds.empty())
        {
            return ddsHasOpaqueAlphaExpected(image.dds);
        }
        if (image.width == 0 || image.height == 0 || image.pixels.size() % 4 != 0 ||
            image.pixels.size() / 4 != uint64_t(image.width) * image.height)
        {
            return false;
        }
        for (size_t i = 3; i < image.pixels.size(); i += 4)
        {
            if (image.pixels[i] != 255)
            {
                return false;
            }
        }
        return true;
    }

    std::expected<void, std::string> asset_detail::removeOpaqueOrcaCutoutsExpected(SceneData& scene)
    {
        std::vector<bool> candidates(scene.materials.size());
        for (size_t i = 0; i < scene.materials.size(); ++i)
        {
            const auto& material = scene.materials[i];
            candidates[i] =
                material.alphaCutoff >= 0 && material.baseColor.a == 1 && material.baseColorTexture.image >= 0;
        }
        // A shared material must remain masked if any referenced corner carries alpha.
        for (const auto& primitive : scene.primitives)
        {
            if (!candidates[primitive.material])
            {
                continue;
            }
            for (uint32_t index : std::span(scene.indices).subspan(primitive.firstIndex, primitive.indexCount))
            {
                if (scene.vertices[index].color.a != 1)
                {
                    candidates[primitive.material] = false;
                    break;
                }
            }
        }
        std::vector<int8_t> opaqueImages(scene.images.size(), -1);
        for (size_t i = 0; i < scene.materials.size(); ++i)
        {
            if (!candidates[i])
            {
                continue;
            }
            auto& material = scene.materials[i];
            auto& opaque   = opaqueImages[size_t(material.baseColorTexture.image)];
            if (opaque < 0)
            {
                auto result = imageHasOpaqueAlphaExpected(scene.images[size_t(material.baseColorTexture.image)]);
                if (!result)
                {
                    return std::unexpected(result.error());
                }
                opaque = *result ? 1 : 0;
            }
            if (opaque != 0)
            {
                material.alphaCutoff = -1;
            }
        }
        return {};
    }
} // namespace vultra
