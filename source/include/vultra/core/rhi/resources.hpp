#pragma once
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/device.hpp>

#include <vector>

namespace vultra
{
    // Whole-resource tracking only. Use VRI directly when an experiment needs subresources.
    class Texture
    {
    public:
        Texture(Device&               device,
                const VriTextureDesc& desc,
                VriTexture*           borrowed = nullptr,
                VriImageAspectFlags   aspect   = VriImageAspect_Color);
        ~Texture();
        Texture(const Texture&)                  = delete;
        Texture&       operator=(const Texture&) = delete;
        void           transition(VriCommandBuffer* cmd, VriAccessLayoutStage next);
        VriDescriptor* view();
        VriDescriptor* mipView(uint32_t mip);

        VriTexture*          handle = nullptr;
        VriTextureDesc       desc {};
        VriAccessLayoutStage state {};

    private:
        Device&                     m_Device;
        bool                        m_Owned;
        VriImageAspectFlags         m_Aspect;
        VriDescriptor*              m_View = nullptr;
        std::vector<VriDescriptor*> m_MipViews;
    };

    class Buffer
    {
    public:
        Buffer(Device& device, const VriBufferDesc& desc);
        ~Buffer();
        Buffer(const Buffer&)                   = delete;
        Buffer&        operator=(const Buffer&) = delete;
        void           transition(VriCommandBuffer* cmd, VriAccessStage next);
        VriBuffer*     handle = nullptr;
        VriBufferDesc  desc {};
        VriAccessStage state {};

    private:
        Device& m_Device;
    };

    VriTextureDesc colorTexture(Extent extent, VriFormat format = VriFormat_RGBA8_UNORM);
    VriTextureDesc depthTexture(Extent extent);
    void           beginColorPass(Device&           device,
                                  VriCommandBuffer* cmd,
                                  VriDescriptor*    view,
                                  Extent            extent,
                                  const float*      clearColor = nullptr);
} // namespace vultra
