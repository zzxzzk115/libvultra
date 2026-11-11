#pragma once
#include <vultra/core/image/image.hpp>
#include <vultra/gpu/resources.hpp>

namespace vultra
{
    // Record into the caller's frame. Consume only after that submission has completed.
    class ImageReadback
    {
    public:
        [[nodiscard]] static std::expected<ImageReadback, Error>
                                                  create(Device& device, Texture& texture, uint32_t mip = 0);
        [[nodiscard]] std::expected<void, Error>  record(VriCommandBuffer* cmd, Texture& texture);
        [[nodiscard]] std::expected<Image, Error> consume();

    private:
        ImageReadback(Device& device, Buffer staging, Extent size, VriFormat format, uint32_t mip, uint32_t texelBytes);

        Device&   m_Device;
        Buffer    m_Staging;
        Extent    m_Size;
        VriFormat m_Format;
        uint32_t  m_Mip;
        uint32_t  m_TexelBytes;
        uint64_t  m_RowBytes;
        bool      m_Recorded = false;
    };

    // Blocking readback between frames. Source needs TransferSrc and initialized contents.
    // RGBA/BGRA8, RGBA16/32F and D32F; depth is replicated to RGB. Floats stay floats until savePng quantizes them.
    [[nodiscard]] std::expected<Image, Error> readback(Device& device, Texture& texture, uint32_t mip = 0);
} // namespace vultra
