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

        DXGI_FORMAT normalizeDds(DirectX::ScratchImage& image, std::optional<bool> srgb, bool decompress)
        {
            const bool color  = srgb.value_or(DirectX::IsSRGB(image.GetMetadata().format));
            auto       format = DirectX::MakeLinear(image.GetMetadata().format);
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
            image.OverrideFormat(format);
            return format;
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
        auto        format = normalizeDds(image, srgb, decompress);
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

    TextureData loadDds(const std::filesystem::path& path, const SourceObserver& observer, const AssetSource* source)
    {
        return asset_detail::decodeDds(readSourceFile(path, observer, source), std::nullopt, false);
    }

    TextureAssetData loadTextureAsset(const std::filesystem::path& path,
                                      bool                         srgb,
                                      const SourceObserver&        observer,
                                      const AssetSource*           source)
    {
        const auto       bytes = readSourceFile(path, observer, source);
        TextureAssetData result;
        if (bytes.size() < 4 || std::memcmp(bytes.data(), "DDS ", 4) != 0)
        {
            const auto texture = prepareTexture(loadSceneImage(path, observer, source), srgb);
            result.format      = texture.format;
            result.mips        = uint32_t(texture.levels.size());
            for (uint32_t mip = 0; mip < result.mips; ++mip)
            {
                const auto& level = texture.levels[mip];
                result.subresources.push_back({level.size, 1, mip, 0, level.bytes});
            }
            return result;
        }
        DirectX::ScratchImage image;
        DirectX::TexMetadata  metadata;
        checkDds(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, &metadata, image),
                 "Load shader DDS texture");
        if (metadata.width == 0 || metadata.height == 0 || metadata.depth == 0 || metadata.arraySize == 0 ||
            metadata.width > UINT32_MAX || metadata.height > UINT32_MAX || metadata.depth > UINT32_MAX ||
            metadata.arraySize > 65536 || metadata.mipLevels == 0 || metadata.mipLevels > 32 ||
            metadata.GetAlphaMode() == DirectX::TEX_ALPHA_MODE_PREMULTIPLIED)
        {
            throw std::invalid_argument("Invalid shader DDS dimensions, mip count or premultiplied alpha");
        }
        if (metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
        {
            result.dimension = TextureDimension::e3D;
        }
        else if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D)
        {
            throw std::invalid_argument("Shader textures require a 2D, array, cube or 3D DDS");
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
        result.format     = ddsFormat(format);
        result.layers     = uint32_t(metadata.arraySize);
        result.mips       = uint32_t(metadata.mipLevels);
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
                    throw std::invalid_argument("DDS is missing an authored subresource");
                }
                size_t rowBytes   = 0;
                size_t sliceBytes = 0;
                checkDds(DirectX::ComputePitch(format, source->width, source->height, rowBytes, sliceBytes),
                         "Shader DDS pitch");
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
                        throw std::invalid_argument("DDS is missing a volume slice");
                    }
                    for (size_t row = 0; row < DirectX::ComputeScanlines(format, slice->height); ++row)
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
