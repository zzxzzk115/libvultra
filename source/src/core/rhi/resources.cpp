#include <vultra/core/rhi/resources.hpp>

#include <algorithm>

namespace vultra
{
    Texture::Texture(Device&               device,
                     const VriTextureDesc& description,
                     VriTexture*           borrowed,
                     VriImageAspectFlags   aspect) :
        handle(borrowed),
        desc(description),
        m_Device(device),
        m_Owned(!borrowed),
        m_Aspect(aspect)
    {
        if (m_Owned)
        {
            check(device.core.CreateTexture(device.handle, &desc, &handle), "Create texture");
        }
    }

    Texture::~Texture()
    {
        for (auto* view : m_MipViews)
        {
            if (view)
            {
                m_Device.core.DestroyDescriptor(view);
            }
        }
        if (m_View)
        {
            m_Device.core.DestroyDescriptor(m_View);
        }
        if (m_Owned)
        {
            m_Device.core.DestroyTexture(handle);
        }
    }

    void Texture::transition(VriCommandBuffer* cmd, VriAccessLayoutStage next)
    {
        // Emit even when layouts match: write->write and write->read still need a dependency.
        VriTextureBarrierDesc barrier {};
        barrier.texture = handle;
        barrier.before  = state;
        barrier.after   = next;
        barrier.aspect  = m_Aspect;
        VriBarrierGroupDesc group {};
        group.textures   = &barrier;
        group.textureNum = 1;
        m_Device.core.CmdBarrier(cmd, &group);
        state = next;
    }

    VriDescriptor* Texture::view()
    {
        if (!m_View)
        {
            VriTextureViewDesc vd {};
            vd.texture  = handle;
            vd.viewType = VriTextureViewType_2D;
            vd.format   = desc.format;
            vd.aspect   = m_Aspect;
            vd.layerNum = 1;
            check(m_Device.core.CreateTextureView(m_Device.handle, &vd, &m_View), "Create texture view");
        }
        return m_View;
    }

    VriDescriptor* Texture::mipView(uint32_t mip)
    {
        if (mip >= desc.mipNum)
        {
            throw std::out_of_range("Texture mip view");
        }
        m_MipViews.resize(desc.mipNum, nullptr);
        if (!m_MipViews[mip])
        {
            VriTextureViewDesc view {};
            view.texture  = handle;
            view.viewType = VriTextureViewType_2D;
            view.format   = desc.format;
            view.aspect   = m_Aspect;
            view.baseMip  = mip;
            view.mipNum   = 1;
            view.layerNum = 1;
            check(m_Device.core.CreateTextureView(m_Device.handle, &view, &m_MipViews[mip]), "Create mip view");
        }
        return m_MipViews[mip];
    }

    Buffer::Buffer(Device& device, const VriBufferDesc& description) :
        desc(description),
        m_Device(device)
    {
        check(device.core.CreateBuffer(device.handle, &desc, &handle), "Create buffer");
    }

    Buffer::~Buffer()
    {
        m_Device.core.DestroyBuffer(handle);
    }

    void Buffer::transition(VriCommandBuffer* cmd, VriAccessStage next)
    {
        VriBufferBarrierDesc barrier {handle, state, next};
        VriBarrierGroupDesc  group {};
        group.buffers   = &barrier;
        group.bufferNum = 1;
        m_Device.core.CmdBarrier(cmd, &group);
        state = next;
    }

    VriTextureDesc colorTexture(Extent extent, VriFormat format)
    {
        VriTextureDesc desc {};
        desc.type      = VriTextureType_2D;
        desc.format    = format;
        desc.width     = extent.width;
        desc.height    = extent.height;
        desc.depth     = 1;
        desc.mipNum    = 1;
        desc.layerNum  = 1;
        desc.sampleNum = 1;
        desc.usage = VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResource | VriTextureUsage_TransferSrc |
                     VriTextureUsage_TransferDst;
        desc.memoryLocation = VriMemoryLocation_Device;
        return desc;
    }

    VriTextureDesc depthTexture(Extent extent)
    {
        auto desc                          = colorTexture(extent, VriFormat_D32_SFLOAT);
        desc.usage                         = VriTextureUsage_DepthStencilAttachment | VriTextureUsage_ShaderResource;
        desc.clearValue.depthStencil.depth = 1;
        return desc;
    }

    void
    beginColorPass(Device& device, VriCommandBuffer* cmd, VriDescriptor* view, Extent extent, const float* clearColor)
    {
        VriAttachmentDesc color {};
        color.view    = view;
        color.loadOp  = clearColor ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
        color.storeOp = VriAttachmentStoreOp_Store;
        if (clearColor)
        {
            std::copy_n(clearColor, 4, color.clearValue.color.f32);
        }
        VriAttachmentsDesc attachments {};
        attachments.colors     = &color;
        attachments.colorNum   = 1;
        attachments.renderArea = {0, 0, extent.width, extent.height};
        attachments.layerNum   = 1;
        device.core.CmdBeginRendering(cmd, &attachments);
        VriViewport viewport {0, 0, float(extent.width), float(extent.height), 0, 1};
        VriRect     scissor {0, 0, extent.width, extent.height};
        device.core.CmdSetViewports(cmd, &viewport, 1);
        device.core.CmdSetScissors(cmd, &scissor, 1);
    }
} // namespace vultra
