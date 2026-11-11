#include "image_decode.hpp"

#include <vultra/assets/texture_asset.hpp>

#include <DirectXTex.h>

#include <cstring>
#include <format>
#include <limits>

namespace vultra
{
    namespace
    {
        std::expected<void, Error> checkDds(HRESULT result, const char* operation)
        {
            if (FAILED(result))
            {
                return makeError("{}: HRESULT 0x{:08x}", operation, uint32_t(result));
            }
            return {};
        }

        std::expected<void, Error> validateDds(const DirectX::TexMetadata& metadata)
        {
            if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 ||
                metadata.IsCubemap() || metadata.depth != 1)
            {
                return makeError("DDS requires a single 2D texture; arrays, cubemaps and volumes are unsupported");
            }
            if (metadata.GetAlphaMode() == DirectX::TEX_ALPHA_MODE_PREMULTIPLIED)
            {
                return makeError("Premultiplied DDS alpha is unsupported by straight-alpha material slots");
            }
            if (metadata.width == 0 || metadata.height == 0 || metadata.width > std::numeric_limits<uint32_t>::max() ||
                metadata.height > std::numeric_limits<uint32_t>::max() || metadata.mipLevels == 0 ||
                metadata.mipLevels > 32)
            {
                return makeError("Invalid DDS dimensions or mip count");
            }
            return {};
        }

        std::expected<TextureFormat, Error> ddsFormat(DXGI_FORMAT format)
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
                    return makeError("Unsupported DDS DXGI format {}", int(format));
            }
        }

        std::expected<DXGI_FORMAT, Error>
        normalizeDds(DirectX::ScratchImage& image, std::optional<bool> srgb, bool decompress)
        {
            const bool color  = srgb.value_or(DirectX::IsSRGB(image.GetMetadata().format));
            auto       format = DirectX::MakeLinear(image.GetMetadata().format);
            // Reject unsupported signed/integer formats before conversion.
            if (auto supported = ddsFormat(format); !supported)
            {
                return std::unexpected(supported.error());
            }
            if (color && DirectX::MakeSRGB(format) == format)
            {
                return makeError("DDS format has no sRGB interpretation for a color material slot");
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
                if (auto result = checkDds(DirectX::Decompress(image.GetImages(),
                                                               image.GetImageCount(),
                                                               image.GetMetadata(),
                                                               output,
                                                               decoded),
                                           "Decompress DDS for hardware sRGB sampling or uncompressed import");
                    !result)
                {
                    return std::unexpected(result.error());
                }
                image  = std::move(decoded);
                format = output;
            }
            if (color)
            {
                format = DirectX::MakeSRGB(format);
            }
            image.OverrideFormat(format);
            return format;
        }
    } // namespace

    std::expected<bool, Error> asset_detail::ddsHasOpaqueAlpha(std::span<const std::byte> bytes)
    {
        DirectX::TexMetadata metadata;
        if (auto result = checkDds(
                DirectX::GetMetadataFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, metadata),
                "Read DDS alpha metadata");
            !result)
        {
            return std::unexpected(result.error());
        }
        if (auto result = validateDds(metadata); !result)
        {
            return std::unexpected(result.error());
        }
        // Only prove formats whose decoded alpha is an exact UNORM byte. Other
        // formats keep their cutout policy and use the normal import validation.
        switch (DirectX::MakeLinear(metadata.format))
        {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC2_UNORM:
            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC7_UNORM:
                break;
            default:
                return false;
        }
        const auto texture = decodeDds(bytes, false, true);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        // Reuse the texture decoder so partial BC blocks, authored lower mips and
        // malformed payloads have the same semantics as the uploaded texture.
        for (const auto& level : texture->levels)
        {
            for (size_t i = 3; i < level.bytes.size(); i += 4)
            {
                if (level.bytes[i] != std::byte(255))
                {
                    return false;
                }
            }
        }
        return true;
    }

    std::expected<SceneImage, Error> asset_detail::retainDds(std::span<const std::byte> bytes)
    {
        DirectX::TexMetadata metadata;
        if (auto result = checkDds(
                DirectX::GetMetadataFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, metadata),
                "Read DDS metadata");
            !result)
        {
            return std::unexpected(result.error());
        }
        if (auto result = validateDds(metadata); !result)
        {
            return std::unexpected(result.error());
        }
        return SceneImage {uint32_t(metadata.width), uint32_t(metadata.height), {}, {bytes.begin(), bytes.end()}};
    }

    std::expected<TextureData, Error>
    asset_detail::decodeDds(std::span<const std::byte> bytes, std::optional<bool> srgb, bool decompress)
    {
        DirectX::ScratchImage image;
        DirectX::TexMetadata  metadata;
        if (auto result = checkDds(
                DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, &metadata, image),
                "Load DDS mip chain");
            !result)
        {
            return std::unexpected(result.error());
        }
        if (auto result = validateDds(metadata); !result)
        {
            return std::unexpected(result.error());
        }
        const auto format = normalizeDds(image, srgb, decompress);
        if (!format)
        {
            return std::unexpected(format.error());
        }
        const auto textureFormat = ddsFormat(*format);
        if (!textureFormat)
        {
            return std::unexpected(textureFormat.error());
        }
        TextureData result;
        result.format = *textureFormat;
        for (size_t mip = 0; mip < metadata.mipLevels; ++mip)
        {
            const auto* source     = image.GetImage(mip, 0, 0);
            size_t      rowBytes   = 0;
            size_t      sliceBytes = 0;
            if (auto pitch =
                    checkDds(DirectX::ComputePitch(*format, source->width, source->height, rowBytes, sliceBytes),
                             "DDS pitch");
                !pitch)
            {
                return std::unexpected(pitch.error());
            }
            TextureLevel level {{uint32_t(source->width), uint32_t(source->height)},
                                std::vector<std::byte>(sliceBytes)};
            for (size_t row = 0; row < DirectX::ComputeScanlines(*format, source->height); ++row)
            {
                std::memcpy(level.bytes.data() + row * rowBytes, source->pixels + row * source->rowPitch, rowBytes);
            }
            result.levels.push_back(std::move(level));
        }
        return result;
    }

    std::expected<TextureData, Error>
    loadDds(const std::filesystem::path& path, const SourceObserver& observer, const AssetSource* source)
    {
        const auto bytes = readSourceFile(path, observer, source);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        return asset_detail::decodeDds(*bytes, std::nullopt, false);
    }

    std::expected<TextureAssetData, Error> loadTextureAsset(const std::filesystem::path& path,
                                                            bool                         srgb,
                                                            const SourceObserver&        observer,
                                                            const AssetSource*           source)
    {
        const auto bytes = readSourceFile(path, observer, source);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        TextureAssetData result;
        if (bytes->size() < 4 || std::memcmp(bytes->data(), "DDS ", 4) != 0)
        {
            const auto image = loadSceneImage(path, observer, source);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            const auto texture = prepareTexture(*image, srgb);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            result.format = texture->format;
            result.mips   = uint32_t(texture->levels.size());
            for (uint32_t mip = 0; mip < result.mips; ++mip)
            {
                const auto& level = texture->levels[mip];
                result.subresources.push_back({level.size, 1, mip, 0, level.bytes});
            }
            return result;
        }
        DirectX::ScratchImage image;
        DirectX::TexMetadata  metadata;
        if (auto loaded = checkDds(
                DirectX::LoadFromDDSMemory(bytes->data(), bytes->size(), DirectX::DDS_FLAGS_NONE, &metadata, image),
                "Load shader DDS texture");
            !loaded)
        {
            return std::unexpected(loaded.error());
        }
        if (metadata.width == 0 || metadata.height == 0 || metadata.depth == 0 || metadata.arraySize == 0 ||
            metadata.width > UINT32_MAX || metadata.height > UINT32_MAX || metadata.depth > UINT32_MAX ||
            metadata.arraySize > 65536 || metadata.mipLevels == 0 || metadata.mipLevels > 32 ||
            metadata.GetAlphaMode() == DirectX::TEX_ALPHA_MODE_PREMULTIPLIED)
        {
            return makeError("Invalid shader DDS dimensions, mip count or premultiplied alpha");
        }
        if (metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
        {
            result.dimension = TextureDimension::e3D;
        }
        else if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D)
        {
            return makeError("Shader textures require a 2D, array, cube or 3D DDS");
        }
        else if (metadata.IsCubemap())
        {
            result.dimension = metadata.arraySize == 6 ? TextureDimension::eCube : TextureDimension::eCubeArray;
        }
        else if (metadata.arraySize > 1)
        {
            result.dimension = TextureDimension::e2DArray;
        }
        const auto format = normalizeDds(image, srgb, false);
        if (!format)
        {
            return std::unexpected(format.error());
        }
        const auto textureFormat = ddsFormat(*format);
        if (!textureFormat)
        {
            return std::unexpected(textureFormat.error());
        }
        result.format = *textureFormat;
        result.layers = uint32_t(metadata.arraySize);
        result.mips   = uint32_t(metadata.mipLevels);
        for (uint32_t layer = 0; layer < result.layers; ++layer)
        {
            for (uint32_t mip = 0; mip < result.mips; ++mip)
            {
                const auto  depth  = result.dimension == TextureDimension::e3D ?
                                         uint32_t(std::max(metadata.depth >> mip, size_t(1))) :
                                         1u;
                const auto* source = image.GetImage(mip, layer, 0);
                if (!source)
                {
                    return makeError("DDS is missing an authored subresource");
                }
                size_t rowBytes   = 0;
                size_t sliceBytes = 0;
                if (auto pitch =
                        checkDds(DirectX::ComputePitch(*format, source->width, source->height, rowBytes, sliceBytes),
                                 "Shader DDS pitch");
                    !pitch)
                {
                    return std::unexpected(pitch.error());
                }
                TextureSubresource resource {{uint32_t(source->width), uint32_t(source->height)},
                                             depth,
                                             mip,
                                             layer,
                                             std::vector<std::byte>(sliceBytes * depth)};
                for (uint32_t z = 0; z < depth; ++z)
                {
                    const auto* slice = image.GetImage(mip, layer, z);
                    if (!slice)
                    {
                        return makeError("DDS is missing a volume slice");
                    }
                    for (size_t row = 0; row < DirectX::ComputeScanlines(*format, slice->height); ++row)
                    {
                        std::memcpy(resource.bytes.data() + z * sliceBytes + row * rowBytes,
                                    slice->pixels + row * slice->rowPitch,
                                    rowBytes);
                    }
                }
                result.subresources.push_back(std::move(resource));
            }
        }
        return result;
    }
} // namespace vultra
