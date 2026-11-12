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

        bool sameState(const VriAccessLayoutStage& a, const VriAccessLayoutStage& b)
        {
            return a.access == b.access && a.layout == b.layout && a.stages == b.stages;
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
        m_Aspect(aspect),
        m_States(size_t(std::max(description.mipNum, 1u)) * layerCount())
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
        m_Device(other.m_Device),
        m_Owned(std::exchange(other.m_Owned, false)),
        m_Aspect(other.m_Aspect),
        m_States(std::move(other.m_States)),
        m_Views(std::exchange(other.m_Views, {}))
    {
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other)
        {
            release();
            handle   = std::exchange(other.handle, nullptr);
            desc     = other.desc;
            m_Device = other.m_Device;
            m_Owned  = std::exchange(other.m_Owned, false);
            m_Aspect = other.m_Aspect;
            m_States = std::move(other.m_States);
            m_Views  = std::exchange(other.m_Views, {});
        }
        return *this;
    }

    Texture::~Texture()
    {
        release();
    }

    void Texture::release()
    {
        for (const auto& view : m_Views)
        {
            m_Device->core.DestroyDescriptor(view.descriptor);
        }
        m_Views.clear();
        if (m_Owned && handle)
        {
            m_Device->core.DestroyTexture(handle);
        }
        handle  = nullptr;
        m_Owned = false;
    }

    void Texture::discardContents()
    {
        std::ranges::fill(m_States, VriAccessLayoutStage {});
    }

    uint32_t Texture::layerCount() const
    {
        return desc.type == VriTextureType_3D ? 1 : std::max(desc.layerNum, 1u);
    }

    VriAccessLayoutStage Texture::state(uint32_t mip, uint32_t layer) const
    {
        if (mip >= std::max(desc.mipNum, 1u) || layer >= layerCount())
        {
            panic("Texture state of a subresource outside the texture");
        }
        return m_States[size_t(mip) * layerCount() + layer];
    }

    void Texture::transition(VriCommandBuffer* cmd, VriAccessLayoutStage next, TextureRange part)
    {
        const uint32_t layers     = layerCount();
        const uint32_t firstMip   = part.mip.value_or(0);
        const uint32_t mipEnd     = part.mip ? firstMip + 1 : std::max(desc.mipNum, 1u);
        const uint32_t firstLayer = part.layer.value_or(0);
        const uint32_t layerEnd   = part.layer ? firstLayer + 1 : layers;
        if (mipEnd > std::max(desc.mipNum, 1u) || layerEnd > layers)
        {
            panic("Texture transition of a subresource outside the texture");
        }
        const auto before  = m_States[size_t(firstMip) * layers + firstLayer];
        bool       uniform = true;
        for (uint32_t mip = firstMip; mip < mipEnd && uniform; ++mip)
        {
            for (uint32_t layer = firstLayer; layer < layerEnd && uniform; ++layer)
            {
                uniform = sameState(m_States[size_t(mip) * layers + layer], before);
            }
        }
        // Emit even when layouts match: write->write and write->read still need a dependency.
        std::vector<VriTextureBarrierDesc> barriers;
        if (uniform)
        {
            // Counts of zero cover every remaining mip or layer, as a whole-texture barrier always has.
            barriers.push_back(
                {handle, before, next, m_Aspect, firstMip, part.mip ? 1u : 0u, firstLayer, part.layer ? 1u : 0u});
        }
        else
        {
            for (uint32_t mip = firstMip; mip < mipEnd; ++mip)
            {
                for (uint32_t layer = firstLayer; layer < layerEnd; ++layer)
                {
                    barriers.push_back(
                        {handle, m_States[size_t(mip) * layers + layer], next, m_Aspect, mip, 1, layer, 1});
                }
            }
        }
        VriBarrierGroupDesc group {};
        group.textures   = barriers.data();
        group.textureNum = uint32_t(barriers.size());
        m_Device->core.CmdBarrier(cmd, &group);
        for (uint32_t mip = firstMip; mip < mipEnd; ++mip)
        {
            std::fill_n(m_States.begin() + ptrdiff_t(size_t(mip) * layers + firstLayer), layerEnd - firstLayer, next);
        }
    }

    std::expected<VriDescriptor*, Error> Texture::view(TextureRange part, std::optional<VriTextureViewType> type)
    {
        if ((part.mip && *part.mip >= std::max(desc.mipNum, 1u)) ||
            (part.layer && (desc.type == VriTextureType_3D || *part.layer >= layerCount())))
        {
            return makeError("Create texture view: the mip or layer is outside the texture");
        }
        const auto resolved = type.value_or(part.layer ? VriTextureViewType_2D : viewType(desc.type));
        const auto cached   = std::ranges::find_if(m_Views,
                                                 [&](const View& view)
                                                 {
                                                     return view.part.mip == part.mip &&
                                                            view.part.layer == part.layer && view.type == resolved;
                                                 });
        if (cached != m_Views.end())
        {
            return cached->descriptor;
        }
        VriTextureViewDesc description {};
        description.texture       = handle;
        description.viewType      = resolved;
        description.format        = desc.format;
        description.aspect        = m_Aspect;
        description.baseMip       = part.mip.value_or(0);
        description.mipNum        = part.mip ? 1 : 0;
        description.baseLayer     = part.layer.value_or(0);
        description.layerNum      = part.layer ? 1 : desc.layerNum;
        VriDescriptor* descriptor = nullptr;
        if (auto created = check(m_Device->core.CreateTextureView(m_Device->handle, &description, &descriptor),
                                 "Create texture view");
            !created)
        {
            return std::unexpected(created.error());
        }
        m_Views.push_back({part, resolved, descriptor});
        return descriptor;
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
