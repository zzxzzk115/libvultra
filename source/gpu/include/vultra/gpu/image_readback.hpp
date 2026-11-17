#pragma once
#include <vultra/core/image/image.hpp>
#include <vultra/gpu/resources.hpp>

namespace vultra
{
    // The texels of one mip and layer as the texture stores them, rows from the top, without padding.
    struct TexelImage
    {
        Extent                 size;
        VriFormat              format     = VriFormat_Unknown;
        uint32_t               texelBytes = 0;
        std::vector<std::byte> texels;
    };

    // Reads one mip of one layer: an array layer or cube face, or a depth slice of a 3D texture's mip.
    // Record into the caller's frame. Consume only after that submission has completed.
    class ImageReadback
    {
    public:
        [[nodiscard]] static std::expected<ImageReadback, Error>
        create(Device& device, Texture& texture, uint32_t mip = 0, uint32_t layer = 0);
        [[nodiscard]] std::expected<void, Error> record(VriCommandBuffer* cmd, Texture& texture);
        // Decodes the texels to floats; integer formats are refused, read them with consumeTexels().
        [[nodiscard]] std::expected<Image, Error>      consume();
        [[nodiscard]] std::expected<TexelImage, Error> consumeTexels();

    private:
        ImageReadback(Device&   device,
                      Buffer    staging,
                      Extent    size,
                      VriFormat format,
                      uint32_t  mip,
                      uint32_t  layer,
                      uint32_t  texelBytes);

        Device&   m_Device;
        Buffer    m_Staging;
        Extent    m_Size;
        VriFormat m_Format;
        uint32_t  m_Mip;
        uint32_t  m_Layer;
        uint32_t  m_TexelBytes;
        uint64_t  m_RowBytes;
        bool      m_Recorded = false;
    };

    // Blocking readback between frames. Source needs TransferSrc and initialized contents.
    // RGBA/BGRA8, RG8, RGBA16, RGBA16/32F, RG16/32F, R16/32F and D32F; depth is replicated to RGB, two-channel
    // formats fill red and green and one-channel formats red only, with alpha 1. Floats stay floats until savePng
    // quantizes them.
    [[nodiscard]] std::expected<Image, Error>
    readback(Device& device, Texture& texture, uint32_t mip = 0, uint32_t layer = 0);
    // The same for the formats above and the unsigned integer formats R8 to RGBA32_UINT, as stored.
    [[nodiscard]] std::expected<TexelImage, Error>
    readbackTexels(Device& device, Texture& texture, uint32_t mip = 0, uint32_t layer = 0);
} // namespace vultra
