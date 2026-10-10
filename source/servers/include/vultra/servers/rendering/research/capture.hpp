#pragma once
#include <vultra/core/image/image.hpp>
#include <vultra/drivers/rhi/resources.hpp>

namespace vultra
{
    // Record into the caller's frame. Consume only after that submission has completed.
    class ImageReadback
    {
    public:
        ImageReadback(Device& device, Texture& texture, uint32_t mip = 0);
        void  record(VriCommandBuffer* cmd, Texture& texture);
        Image consume();

    private:
        Device&   m_Device;
        Buffer    m_Staging;
        Extent    m_Size;
        VriFormat m_Format;
        uint32_t  m_Mip;
        uint32_t  m_TexelBytes;
        bool      m_Recorded = false;
    };

    // Blocking readback between frames. Source needs TransferSrc and initialized contents.
    // RGBA/BGRA8, RGBA16/32F and D32F; depth is replicated to RGB. Floats stay floats until savePng quantizes them.
    Image readback(Device& device, Texture& texture, uint32_t mip = 0);
} // namespace vultra
