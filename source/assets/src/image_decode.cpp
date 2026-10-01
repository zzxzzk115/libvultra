#include "image_decode.hpp"

// Private decoder instance; research PNG writing has a separate implementation.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <limits>

namespace vultra
{
    SceneImage asset_detail::decodeImage(std::span<const std::byte> bytes)
    {
        if (bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) == 0)
        {
            return retainDds(bytes);
        }
        if (bytes.empty() || bytes.size() > size_t(std::numeric_limits<int>::max()))
        {
            throw std::runtime_error("Invalid encoded image size");
        }
        const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
        const int   size = int(bytes.size());
        if (stbi_is_16_bit_from_memory(data, size) || stbi_is_hdr_from_memory(data, size))
        {
            throw std::runtime_error("Material images require 8-bit channels; use Environment for HDR images");
        }
        int                                                        width    = 0;
        int                                                        height   = 0;
        int                                                        channels = 0;
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(data, size, &width, &height, &channels, 4),
            &stbi_image_free);
        if (!pixels)
        {
            throw std::runtime_error(std::string("Decode image: ") + stbi_failure_reason());
        }
        return {uint32_t(width), uint32_t(height), {pixels.get(), pixels.get() + size_t(width) * height * 4}, {}};
    }

    SceneImage loadSceneImage(const std::filesystem::path& path, const SourceObserver& observer)
    {
        try
        {
            return asset_detail::decodeImage(readSourceFile(path, observer));
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("Image " + path.string() + ": " + error.what());
        }
    }
} // namespace vultra
