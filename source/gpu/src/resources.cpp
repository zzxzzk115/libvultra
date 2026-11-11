#include <vultra/gpu/resources.hpp>

#include <algorithm>
#include <utility>

namespace vultra
{
    namespace
    {
        VriTextureViewType viewType(VriTextureType type)
        {
            switch (type)
            {
                case VriTextureType_2DArray:
                    return VriTextureViewType_2DArray;
                case VriTextureType_3D:
                    return VriTextureViewType_3D;
                case VriTextureType_Cube:
                    return VriTextureViewType_Cube;
                case VriTextureType_CubeArray:
                    return VriTextureViewType_CubeArray;
                default:
                    return VriTextureViewType_2D;
            }
        }
    } // namespace

    Texture::Texture(Device&               device,
                     const VriTextureDesc& description,
                     VriTexture*           texture,
                     bool                  owned,
                     VriImageAspectFlags   aspect) :
        handle(texture),
        desc(description),
        m_Device(&device),
        m_Owned(owned),
        m_Aspect(aspect)
    {
    }

    std::expected<Texture, Error>
    Texture::create(Device& device, const VriTextureDesc& description, VriTexture* borrowed, VriImageAspectFlags aspect)
    {
        if (borrowed)
        {
            return Texture(device, description, borrowed, false, aspect);
        }
        VriTexture* texture = nullptr;
        if (auto created = check(device.core.CreateTexture(device.handle, &description, &texture), "Create texture");
            !created)
        {
            return std::unexpected(created.error());
        }
        return Texture(device, description, texture, true, aspect);
    }

    Texture::Texture(Texture&& other) noexcept :
        handle(std::exchange(other.handle, nullptr)),
        desc(other.desc),
        state(other.state),
        m_Device(other.m_Device),
        m_Owned(std::exchange(other.m_Owned, false)),
        m_Aspect(other.m_Aspect),
        m_View(std::exchange(other.m_View, nullptr)),
        m_MipViews(std::exchange(other.m_MipViews, {}))
    {
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other)
        {
            release();
            handle     = std::exchange(other.handle, nullptr);
            desc       = other.desc;
            state      = other.state;
            m_Device   = other.m_Device;
            m_Owned    = std::exchange(other.m_Owned, false);
            m_Aspect   = other.m_Aspect;
            m_View     = std::exchange(other.m_View, nullptr);
            m_MipViews = std::exchange(other.m_MipViews, {});
        }
        return *this;
    }

    Texture::~Texture()
    {
        release();
    }

    void Texture::release()
    {
        for (auto* view : m_MipViews)
        {
            if (view)
            {
                m_Device->core.DestroyDescriptor(view);
            }
        }
        m_MipViews.clear();
        if (m_View)
        {
            m_Device->core.DestroyDescriptor(m_View);
            m_View = nullptr;
        }
        if (m_Owned && handle)
        {
            m_Device->core.DestroyTexture(handle);
        }
        handle  = nullptr;
        m_Owned = false;
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
        m_Device->core.CmdBarrier(cmd, &group);
        state = next;
    }

    std::expected<VriDescriptor*, Error> Texture::view()
    {
        if (!m_View)
        {
            VriTextureViewDesc vd {};
            vd.texture  = handle;
            vd.viewType = viewType(desc.type);
            vd.format   = desc.format;
            vd.aspect   = m_Aspect;
            vd.layerNum = desc.layerNum;
            if (auto created =
                    check(m_Device->core.CreateTextureView(m_Device->handle, &vd, &m_View), "Create texture view");
                !created)
            {
                return std::unexpected(created.error());
            }
        }
        return m_View;
    }

    std::expected<VriDescriptor*, Error> Texture::mipView(uint32_t mip)
    {
        if (mip >= desc.mipNum)
        {
            return makeError("Texture mip view");
        }
        m_MipViews.resize(desc.mipNum, nullptr);
        if (!m_MipViews[mip])
        {
            VriTextureViewDesc view {};
            view.texture  = handle;
            view.viewType = viewType(desc.type);
            view.format   = desc.format;
            view.aspect   = m_Aspect;
            view.baseMip  = mip;
            view.mipNum   = 1;
            view.layerNum = desc.layerNum;
            if (auto created = check(m_Device->core.CreateTextureView(m_Device->handle, &view, &m_MipViews[mip]),
                                     "Create mip view");
                !created)
            {
                return std::unexpected(created.error());
            }
        }
        return m_MipViews[mip];
    }

    Buffer::Buffer(Device& device, const VriBufferDesc& description, VriBuffer* buffer) :
        handle(buffer),
        desc(description),
        m_Device(&device)
    {
    }

    std::expected<Buffer, Error> Buffer::create(Device& device, const VriBufferDesc& description)
    {
        VriBuffer* buffer = nullptr;
        if (auto created = check(device.core.CreateBuffer(device.handle, &description, &buffer), "Create buffer");
            !created)
        {
            return std::unexpected(created.error());
        }
        return Buffer(device, description, buffer);
    }

    Buffer::Buffer(Buffer&& other) noexcept :
        handle(std::exchange(other.handle, nullptr)),
        desc(other.desc),
        state(other.state),
        m_Device(other.m_Device)
    {
    }

    Buffer& Buffer::operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            if (handle)
            {
                m_Device->core.DestroyBuffer(handle);
            }
            handle   = std::exchange(other.handle, nullptr);
            desc     = other.desc;
            state    = other.state;
            m_Device = other.m_Device;
        }
        return *this;
    }

    Buffer::~Buffer()
    {
        if (handle)
        {
            m_Device->core.DestroyBuffer(handle);
        }
    }

    void Buffer::transition(VriCommandBuffer* cmd, VriAccessStage next)
    {
        VriBufferBarrierDesc barrier {handle, state, next};
        VriBarrierGroupDesc  group {};
        group.buffers   = &barrier;
        group.bufferNum = 1;
        m_Device->core.CmdBarrier(cmd, &group);
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
