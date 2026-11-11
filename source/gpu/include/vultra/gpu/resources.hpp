#pragma once
#include <vultra/core/math/extent.hpp>
#include <vultra/gpu/device.hpp>

#include <vector>

namespace vultra
{
    // Whole-resource tracking only. Use VRI directly when an experiment needs subresources.
    // Moving transfers ownership of the VRI texture and its views; the moved-from texture is empty.
    class Texture
    {
    public:
        // A borrowed texture is wrapped without creating anything.
        [[nodiscard]] static std::expected<Texture, Error> create(Device&               device,
                                                                  const VriTextureDesc& desc,
                                                                  VriTexture*           borrowed = nullptr,
                                                                  VriImageAspectFlags   aspect = VriImageAspect_Color);
        ~Texture();
        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&& other) noexcept;
        Texture(const Texture&)            = delete;
        Texture& operator=(const Texture&) = delete;
        void     transition(VriCommandBuffer* cmd, VriAccessLayoutStage next);
        // Views are created on first use and owned by the texture.
        [[nodiscard]] std::expected<VriDescriptor*, Error> view();
        [[nodiscard]] std::expected<VriDescriptor*, Error> mipView(uint32_t mip);

        VriTexture*          handle = nullptr;
        VriTextureDesc       desc {};
        VriAccessLayoutStage state {};

    private:
        Texture(Device& device, const VriTextureDesc& desc, VriTexture* handle, bool owned, VriImageAspectFlags aspect);
        void release();

        Device*                     m_Device;
        bool                        m_Owned;
        VriImageAspectFlags         m_Aspect;
        VriDescriptor*              m_View = nullptr;
        std::vector<VriDescriptor*> m_MipViews;
    };

    // Moving transfers ownership of the VRI buffer; the moved-from buffer is empty.
    class Buffer
    {
    public:
        [[nodiscard]] static std::expected<Buffer, Error> create(Device& device, const VriBufferDesc& desc);
        ~Buffer();
        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;
        Buffer(const Buffer&)                   = delete;
        Buffer&        operator=(const Buffer&) = delete;
        void           transition(VriCommandBuffer* cmd, VriAccessStage next);
        VriBuffer*     handle = nullptr;
        VriBufferDesc  desc {};
        VriAccessStage state {};

    private:
        Buffer(Device& device, const VriBufferDesc& desc, VriBuffer* handle);

        Device* m_Device;
    };

    VriTextureDesc colorTexture(Extent extent, VriFormat format = VriFormat_RGBA8_UNORM);
    VriTextureDesc depthTexture(Extent extent);
    void           beginColorPass(Device&           device,
                                  VriCommandBuffer* cmd,
                                  VriDescriptor*    view,
                                  Extent            extent,
                                  const float*      clearColor = nullptr);
} // namespace vultra
