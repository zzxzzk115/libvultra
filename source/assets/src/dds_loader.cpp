#include "image_decode.hpp"

#include <DirectXTex.h>

#include <cstring>
#include <format>
#include <limits>

namespace vultra
{
    namespace
    {
        void checkDds(HRESULT result, const char* operation)
        {
            if (FAILED(result))
            {
                throw std::runtime_error(std::format("{}: HRESULT 0x{:08x}", operation, uint32_t(result)));
            }
        }

        void validateDds(const DirectX::TexMetadata& metadata)
        {
            if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 ||
                metadata.IsCubemap() || metadata.depth != 1)
            {
                throw std::runtime_error(
                    "DDS requires a single 2D texture; arrays, cubemaps and volumes are unsupported");
            }
            if (metadata.GetAlphaMode() == DirectX::TEX_ALPHA_MODE_PREMULTIPLIED)
            {
                throw std::runtime_error("Premultiplied DDS alpha is unsupported by straight-alpha material slots");
            }
            if (metadata.width == 0 || metadata.height == 0 || metadata.width > std::numeric_limits<uint32_t>::max() ||
                metadata.height > std::numeric_limits<uint32_t>::max() || metadata.mipLevels == 0 ||
                metadata.mipLevels > 32)
            {
                throw std::runtime_error("Invalid DDS dimensions or mip count");
            }
        }

        TextureFormat ddsFormat(DXGI_FORMAT format)
        {
            switch (format)
            {
                case DXGI_FORMAT_R8_UNORM:
                    return TextureFormat::eR8Unorm;
                case DXGI_FORMAT_R8G8_UNORM:
                    return TextureFormat::eRg8Unorm;
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                    return TextureFormat::eRgba8Unorm;
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                    return TextureFormat::eRgba8Srgb;
                case DXGI_FORMAT_B8G8R8A8_UNORM:
                    return TextureFormat::eBgra8Unorm;
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                    return TextureFormat::eBgra8Srgb;
                case DXGI_FORMAT_R16G16B16A16_FLOAT:
                    return TextureFormat::eRgba16Sfloat;
                case DXGI_FORMAT_R32G32B32A32_FLOAT:
                    return TextureFormat::eRgba32Sfloat;
                case DXGI_FORMAT_BC1_UNORM:
                    return TextureFormat::eBc1Unorm;
                case DXGI_FORMAT_BC2_UNORM:
                    return TextureFormat::eBc2Unorm;
                case DXGI_FORMAT_BC3_UNORM:
                    return TextureFormat::eBc3Unorm;
                case DXGI_FORMAT_BC4_UNORM:
                    return TextureFormat::eBc4Unorm;
                case DXGI_FORMAT_BC5_UNORM:
                    return TextureFormat::eBc5Unorm;
                case DXGI_FORMAT_BC6H_UF16:
                    return TextureFormat::eBc6hUfloat;
                case DXGI_FORMAT_BC7_UNORM:
                    return TextureFormat::eBc7Unorm;
                default:
                    throw std::runtime_error(std::format("Unsupported DDS DXGI format {}", int(format)));
            }
        }
    } // namespace

    SceneImage asset_detail::retainDds(std::span<const std::byte> bytes)
    {
        DirectX::TexMetadata metadata;
        checkDds(DirectX::GetMetadataFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, metadata),
                 "Read DDS metadata");
        validateDds(metadata);
        return {uint32_t(metadata.width), uint32_t(metadata.height), {}, {bytes.begin(), bytes.end()}};
    }

    TextureData asset_detail::decodeDds(std::span<const std::byte> bytes, std::optional<bool> srgb, bool decompress)
    {
        DirectX::ScratchImage image;
        DirectX::TexMetadata  metadata;
        checkDds(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, &metadata, image),
                 "Load DDS mip chain");
        validateDds(metadata);
        const bool color  = srgb.value_or(DirectX::IsSRGB(metadata.format));
        auto       format = DirectX::MakeLinear(metadata.format);
        static_cast<void>(ddsFormat(format)); // Reject unsupported signed/integer formats before conversion.
        if (color && DirectX::MakeSRGB(format) == format)
        {
            throw std::runtime_error("DDS format has no sRGB interpretation for a color material slot");
        }
        // Material semantics choose the transfer function. Reinterpret encoded values without gamma conversion.
        image.OverrideFormat(format);
        if (DirectX::IsCompressed(format) && (color || decompress))
        {
            DirectX::ScratchImage decoded;
            auto                  output = DXGI_FORMAT_R8G8B8A8_UNORM;
            if (format == DXGI_FORMAT_BC6H_UF16)
            {
                output = DXGI_FORMAT_R16G16B16A16_FLOAT;
            }
            else if (format == DXGI_FORMAT_BC5_UNORM)
            {
                // Preserve two-channel normal semantics in the uncompressed reference path.
                output = DXGI_FORMAT_R8G8_UNORM;
            }
            checkDds(
                DirectX::Decompress(image.GetImages(), image.GetImageCount(), image.GetMetadata(), output, decoded),
                "Decompress DDS for hardware sRGB sampling or uncompressed import");
            image  = std::move(decoded);
            format = output;
        }
        if (color)
        {
            format = DirectX::MakeSRGB(format);
        }
        TextureData result;
        result.format = ddsFormat(format);
        for (size_t mip = 0; mip < metadata.mipLevels; ++mip)
        {
            const auto* source     = image.GetImage(mip, 0, 0);
            size_t      rowBytes   = 0;
            size_t      sliceBytes = 0;
            checkDds(DirectX::ComputePitch(format, source->width, source->height, rowBytes, sliceBytes), "DDS pitch");
            TextureLevel level {{uint32_t(source->width), uint32_t(source->height)},
                                std::vector<std::byte>(sliceBytes)};
            for (size_t row = 0; row < DirectX::ComputeScanlines(format, source->height); ++row)
            {
                std::memcpy(level.bytes.data() + row * rowBytes, source->pixels + row * source->rowPitch, rowBytes);
            }
            result.levels.push_back(std::move(level));
        }
        return result;
    }

    TextureData loadDds(const std::filesystem::path& path, const SourceObserver& observer)
    {
        return asset_detail::decodeDds(readSourceFile(path, observer), std::nullopt, false);
    }
} // namespace vultra
