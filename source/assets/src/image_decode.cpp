#include "image_decode.hpp"

// Keep the full asset decoder private; Image I/O has its own PNG-only stb implementation.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <limits>

namespace vultra
{
    std::expected<SceneImage, Error> asset_detail::decodeImage(std::span<const std::byte> bytes)
    {
        if (bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) == 0)
        {
            return retainDds(bytes);
        }
        if (bytes.empty() || bytes.size() > size_t(std::numeric_limits<int>::max()))
        {
            return makeError("Invalid encoded image size");
        }
        const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
        const int   size = int(bytes.size());
        if (stbi_is_16_bit_from_memory(data, size) || stbi_is_hdr_from_memory(data, size))
        {
            return makeError("Material images require 8-bit channels; use Environment for HDR images");
        }
        int                                                        width    = 0;
        int                                                        height   = 0;
        int                                                        channels = 0;
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(data, size, &width, &height, &channels, 4),
            &stbi_image_free);
        if (!pixels)
        {
            return makeError("Decode image: {}", stbi_failure_reason());
        }
        return SceneImage {uint32_t(width),
                           uint32_t(height),
                           {pixels.get(), pixels.get() + size_t(width) * height * 4},
                           {}};
    }

    std::expected<SceneImage, Error>
    loadSceneImage(const std::filesystem::path& path, const SourceObserver& observer, const AssetSource* source)
    {
        const auto bytes = readSourceFile(path, observer, source);
        if (!bytes)
        {
            return withContext("Image " + path.string(), bytes.error());
        }
        auto image = asset_detail::decodeImage(*bytes);
        if (!image)
        {
            return withContext("Image " + path.string(), image.error());
        }
        return image;
    }

    std::expected<bool, Error> asset_detail::imageHasOpaqueAlpha(const SceneImage& image)
    {
        if (!image.dds.empty())
        {
            return ddsHasOpaqueAlpha(image.dds);
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

    std::expected<void, Error> asset_detail::removeOpaqueOrcaCutouts(SceneData& scene)
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
                const auto proven = imageHasOpaqueAlpha(scene.images[size_t(material.baseColorTexture.image)]);
                if (!proven)
                {
                    return std::unexpected(proven.error());
                }
                opaque = *proven ? 1 : 0;
            }
            if (opaque != 0)
            {
                material.alphaCutoff = -1;
            }
        }
        return {};
    }
} // namespace vultra
