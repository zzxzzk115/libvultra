#include <vultra/drivers/rhi/resources.hpp>

#include <algorithm>
#include <bit>
#include <cmath>

namespace vultra
{
    namespace
    {
        bool sameState(VriAccessStage a, VriAccessStage b)
        {
            return a.access == b.access && a.stages == b.stages;
        }

        bool sameState(VriAccessLayoutStage a, VriAccessLayoutStage b)
        {
            return a.access == b.access && a.layout == b.layout && a.stages == b.stages;
        }

        bool repeatedTextureRead(VriAccessLayoutStage before, VriAccessLayoutStage after)
        {
            const bool readOnlyDepth = before.layout == VriLayout_DepthStencilReadOnly &&
                                       before.access == VriAccess_DepthStencilAttachmentRead;
            return (before.access == VriAccess_ShaderResourceRead || readOnlyDepth) && sameState(before, after);
        }

        VriImageAspectFlags formatAspects(VriFormat format)
        {
            switch (format)
            {
                case VriFormat_D16_UNORM:
                case VriFormat_D32_SFLOAT:
                    return VriImageAspect_Depth;
                case VriFormat_D24_UNORM_S8_UINT:
                case VriFormat_D32_SFLOAT_S8_UINT:
                    return VriImageAspect_Depth | VriImageAspect_Stencil;
                case VriFormat_S8_UINT:
                    return VriImageAspect_Stencil;
                default:
                    return VriImageAspect_Color;
            }
        }

        VriTextureViewType viewType(const VriTextureDesc& desc)
        {
            switch (desc.type)
            {
                case VriTextureType_1D:
                    return desc.layerNum > 1 ? VriTextureViewType_1DArray : VriTextureViewType_1D;
                case VriTextureType_1DArray:
                    return VriTextureViewType_1DArray;
                case VriTextureType_2D:
                    return desc.layerNum > 1 ? VriTextureViewType_2DArray : VriTextureViewType_2D;
                case VriTextureType_2DArray:
                    return VriTextureViewType_2DArray;
                case VriTextureType_3D:
                    return VriTextureViewType_3D;
                case VriTextureType_Cube:
                    return VriTextureViewType_Cube;
                case VriTextureType_CubeArray:
                    return VriTextureViewType_CubeArray;
                default:
                    return VriTextureViewType_MaxEnum;
            }
        }

        bool compatibleView(const VriTextureDesc& texture, const VriTextureViewDesc& view)
        {
            const auto type = viewType(texture);
            if (texture.sampleNum > 1)
            {
                return view.viewType == type;
            }
            switch (type)
            {
                case VriTextureViewType_1D:
                    return view.viewType == VriTextureViewType_1D && view.layerNum == 1;
                case VriTextureViewType_1DArray:
                    return view.viewType == VriTextureViewType_1DArray;
                case VriTextureViewType_2D:
                case VriTextureViewType_2DArray:
                case VriTextureViewType_Cube:
                case VriTextureViewType_CubeArray:
                    if (view.viewType == VriTextureViewType_2DArray ||
                        (view.viewType == VriTextureViewType_2D && view.layerNum == 1))
                    {
                        return true;
                    }
                    return (type == VriTextureViewType_Cube || type == VriTextureViewType_CubeArray) &&
                           view.baseLayer % 6 == 0 &&
                           ((view.viewType == VriTextureViewType_Cube && view.layerNum == 6) ||
                            (view.viewType == VriTextureViewType_CubeArray && view.layerNum % 6 == 0));
                case VriTextureViewType_3D:
                    return view.viewType == VriTextureViewType_3D;
                default:
                    return false;
            }
        }

    } // namespace

    std::string_view detail::ResourceCallbackRecording::reject(size_t resource, std::string_view reason)
    {
        if (error.empty())
        {
            failedResource = resource;
            error          = reason;
        }
        return error;
    }

    Texture::Texture(Device&               device,
                     const VriTextureDesc& description,
                     VriTexture*           borrowed,
                     VriImageAspectFlags   aspect,
                     Uninitialized) :
        handle(borrowed),
        desc(description),
        m_Device(device),
        m_Owned(!borrowed),
        m_Aspect(aspect ? aspect : formatAspects(description.format))
    {
    }

    std::expected<std::unique_ptr<Texture>, std::string>
    Texture::create(Device& device, const VriTextureDesc& description, VriTexture* borrowed, VriImageAspectFlags aspect)
    {
        if (aspect & ~formatAspects(description.format))
        {
            return std::unexpected("Texture aspect does not belong to its format");
        }
        if ((description.usage & VriTextureUsage_MutableFormat) &&
            (!device.core.GetTextureViewFormatSupport || !device.core.GetTextureViewFormatSupport(device.handle,
                                                                                                  description.format,
                                                                                                  description.format,
                                                                                                  description.usage)))
        {
            return std::unexpected("Native mutable texture formats are unsupported for this format and usage");
        }
        auto texture = std::unique_ptr<Texture>(new Texture(device, description, borrowed, aspect, Uninitialized {}));
        if (!borrowed)
        {
            const auto result = device.core.CreateTexture(device.handle, &texture->desc, &texture->handle);
            if (result != VriResult_Success)
                return std::unexpected("Create texture failed: " + std::to_string(result));
            if (!texture->handle)
                return std::unexpected("Create texture returned no handle");
        }
        return texture;
    }

    Texture::Texture(Device&               device,
                     const VriTextureDesc& description,
                     VriTexture*           borrowed,
                     VriImageAspectFlags   aspect) :
        handle(borrowed),
        desc(description),
        m_Device(device),
        m_Owned(!borrowed),
        m_Aspect(aspect ? aspect : formatAspects(description.format))
    {
        if (m_Owned)
        {
            check(device.core.CreateTexture(device.handle, &desc, &handle), "Create texture");
        }
    }

    Texture::~Texture()
    {
        for (const auto& view : m_Views)
        {
            m_Device.core.DestroyDescriptor(view.descriptor);
        }
        if (m_Owned && handle)
        {
            m_Device.core.DestroyTexture(handle);
        }
    }

    detail::QueueFamilyOwnership::Claim
    detail::QueueFamilyOwnership::check(uint64_t begin, uint64_t end, uint32_t family) const
    {
        if (spans.empty())
        {
            if (uniform == UINT32_MAX)
            {
                return Claim::eUnowned;
            }
            return uniform == family ? Claim::eUnchanged : Claim::eConflict;
        }
        auto     result = Claim::eUnchanged;
        uint64_t offset = 0;
        for (const auto& span : spans)
        {
            if (offset >= end)
            {
                break;
            }
            if (span.end > begin)
            {
                if (span.family == UINT32_MAX)
                {
                    result = Claim::eUnowned;
                }
                else if (span.family != family)
                {
                    return Claim::eConflict;
                }
            }
            offset = span.end;
        }
        return result;
    }

    void detail::QueueFamilyOwnership::claim(uint64_t begin, uint64_t end, uint64_t total, uint32_t family)
    {
        if (begin == 0 && end == total)
        {
            uniform = family;
            spans.clear();
            return;
        }
        if (spans.empty())
        {
            spans.push_back({total, uniform});
        }
        const auto split = [&](uint64_t position)
        {
            if (position && position < total)
            {
                const auto part = std::ranges::lower_bound(spans, position, {}, &Span::end);
                if (part->end != position)
                {
                    spans.insert(part, {position, part->family});
                }
            }
        };
        split(begin);
        split(end);
        uint64_t offset = 0;
        size_t   kept   = 0;
        for (auto span : spans)
        {
            if (begin <= offset && span.end <= end)
            {
                span.family = family;
            }
            offset = span.end;
            if (kept && spans[kept - 1].family == span.family)
            {
                spans[kept - 1].end = span.end;
            }
            else
            {
                spans[kept++] = span;
            }
        }
        spans.resize(kept);
        if (kept == 1)
        {
            uniform = spans.front().family;
            spans.clear();
        }
    }

    bool Texture::State::belongsTo(const Texture& texture) const
    {
        return m_Owner == &texture && m_Handle == texture.handle && m_MipNum == std::max(texture.desc.mipNum, 1u) &&
               m_LayerNum == std::max(texture.desc.layerNum, 1u) && m_Aspects == formatAspects(texture.desc.format);
    }

    bool Texture::State::operator==(const State& other) const
    {
        if (m_Owner != other.m_Owner || m_Handle != other.m_Handle || m_MipNum != other.m_MipNum ||
            m_LayerNum != other.m_LayerNum || m_Aspects != other.m_Aspects)
        {
            return false;
        }
        if (m_Cells.empty() && other.m_Cells.empty())
        {
            return sameState(m_Uniform, other.m_Uniform);
        }
        const size_t count = size_t(m_MipNum) * m_LayerNum * std::popcount(m_Aspects);
        for (size_t i = 0; i < count; ++i)
        {
            if (!sameState(m_Cells.empty() ? m_Uniform : m_Cells[i],
                           other.m_Cells.empty() ? other.m_Uniform : other.m_Cells[i]))
            {
                return false;
            }
        }
        return true;
    }

    Texture::State Texture::captureState() const
    {
        State result;
        result.m_Owner    = this;
        result.m_Handle   = handle;
        result.m_MipNum   = std::max(desc.mipNum, 1u);
        result.m_LayerNum = std::max(desc.layerNum, 1u);
        result.m_Aspects  = formatAspects(desc.format);
        result.m_Uniform  = m_State;
        if (!m_SubresourceStates.empty())
        {
            result.m_Uniform = m_SubresourceStates.front();
            if (!std::ranges::all_of(m_SubresourceStates,
                                     [&](VriAccessLayoutStage state)
                                     {
                                         return sameState(state, result.m_Uniform);
                                     }))
            {
                result.m_Cells = m_SubresourceStates;
            }
        }
        return result;
    }

    std::expected<void, std::string> Texture::restoreUnsubmittedExpected(State state)
    {
        if (!state.belongsTo(*this))
        {
            return std::unexpected("Texture changed or state snapshot belongs to another texture");
        }
        if (m_CallbackAccess && m_CallbackAccess->recording->commands)
        {
            auto& callback = *m_CallbackAccess;
            return std::unexpected(
                std::string(callback.recording->reject(callback.resource,
                                                       "texture callback cannot restore a discarded recording")));
        }
        restoreTrackedState(std::move(state));
        return {};
    }

    void Texture::restoreTrackedState(State state)
    {
        m_State             = state.m_Uniform;
        m_SubresourceStates = std::move(state.m_Cells);
    }

    bool Texture::recordCallbackAccess(VriCommandBuffer*          commands,
                                       VriAccessLayoutStage       next,
                                       VriTextureSubresourceRange range,
                                       VriImageAspectFlags        aspects)
    {
        const auto& callback  = *m_CallbackAccess;
        auto&       recording = *callback.recording;
        if (!recording.commands)
        {
            return true;
        }
        if (!recording.error.empty())
        {
            return false;
        }
        if (commands != recording.commands)
        {
            recording.reject(callback.resource, "texture callback used another recording destination");
            return false;
        }

        if (callback.cells.empty())
        {
            recording.reject(callback.resource, "texture callback accessed an undeclared mip, layer or aspect");
            return false;
        }

        constexpr auto writeAccess = VriAccess_ShaderResourceStorageWrite | VriAccess_ColorAttachmentWrite |
                                     VriAccess_DepthStencilAttachmentWrite | VriAccess_CopyDestinationWrite;
        const bool writes = (next.access & writeAccess) != 0 || next.layout == VriLayout_Undefined;
        const bool preservingStore =
            next.layout == VriLayout_DepthStencilAttachment &&
            next.access == (VriAccess_DepthStencilAttachmentRead | VriAccess_DepthStencilAttachmentWrite);
        const auto available = formatAspects(desc.format);
        const auto planes    = size_t(std::popcount(available));
        for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerNum; ++layer)
        {
            for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipNum; ++mip)
            {
                auto cell = (size_t(layer) * std::max(desc.mipNum, 1u) + mip) * planes;
                for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    if (!(available & aspect))
                    {
                        continue;
                    }
                    const auto index = cell++;
                    if (!(aspects & aspect))
                    {
                        continue;
                    }
                    auto& flags = callback.cells[index];
                    if (!(flags & eDeclared))
                    {
                        recording.reject(callback.resource,
                                         "texture callback accessed an undeclared mip, layer or aspect");
                        return false;
                    }
                    if (writes && !(flags & eWritable) && !(preservingStore && (flags & ePreservingStore)))
                    {
                        recording.reject(callback.resource,
                                         "texture callback requested write access to a read-only mip, layer or aspect");
                        return false;
                    }
                    const auto before = m_SubresourceStates.empty() ? m_State : m_SubresourceStates[index];
                    if (before.layout != next.layout)
                    {
                        flags |= eLayoutChanged;
                    }
                }
            }
        }
        return true;
    }

    void Texture::setState(VriAccessLayoutStage state)
    {
        const VriTextureSubresourceRange range {0, std::max(desc.mipNum, 1u), 0, std::max(desc.layerNum, 1u)};
        const auto                       aspects = formatAspects(desc.format);
        if (m_CallbackAccess && !recordCallbackAccess(m_CallbackAccess->recording->commands, state, range, aspects))
        {
            return;
        }
        m_State = state;
        m_SubresourceStates.clear();
    }

    std::expected<VriAccessLayoutStage, std::string> Texture::stateExpected() const
    {
        if (m_SubresourceStates.empty())
        {
            return m_State;
        }
        const auto state = m_SubresourceStates.front();
        if (std::ranges::all_of(m_SubresourceStates,
                                [&](VriAccessLayoutStage cell)
                                {
                                    return sameState(cell, state);
                                }))
        {
            return state;
        }
        return std::unexpected("Texture subresources have different states; select a mip, layer and aspect");
    }

    std::expected<VriAccessLayoutStage, std::string> Texture::stateExpected(uint32_t mip, uint32_t layer) const
    {
        return stateExpected(mip, layer, formatAspects(desc.format));
    }

    std::expected<VriAccessLayoutStage, std::string>
    Texture::stateExpected(uint32_t mip, uint32_t layer, VriImageAspectFlags aspects) const
    {
        const auto mipNum    = std::max(desc.mipNum, 1u);
        const auto available = formatAspects(desc.format);
        if (mip >= mipNum || layer >= std::max(desc.layerNum, 1u))
        {
            return std::unexpected("Texture state mip or layer is out of range");
        }
        if (!aspects)
        {
            aspects = available;
        }
        if (aspects & ~available)
        {
            return std::unexpected("Texture state aspect does not belong to its format");
        }
        if (m_SubresourceStates.empty())
        {
            return m_State;
        }
        const size_t         base  = (size_t(layer) * mipNum + mip) * std::popcount(available);
        size_t               plane = 0;
        bool                 found = false;
        VriAccessLayoutStage state {};
        for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
        {
            if (!(available & aspect))
            {
                continue;
            }
            if (aspects & aspect)
            {
                const auto current = m_SubresourceStates[base + plane];
                if (found && !sameState(state, current))
                {
                    return std::unexpected("Texture depth/stencil states differ; select one aspect");
                }
                state = current;
                found = true;
            }
            ++plane;
        }
        return state;
    }

    void Texture::transition(VriCommandBuffer* cmd, VriAccessLayoutStage next)
    {
        const VriTextureSubresourceRange range {0, std::max(desc.mipNum, 1u), 0, std::max(desc.layerNum, 1u)};
        const auto                       aspects = formatAspects(desc.format);
        if (m_CallbackAccess && !recordCallbackAccess(cmd, next, range, aspects))
        {
            return;
        }
        transitionRange(cmd, next, range.baseMip, range.mipNum, range.baseLayer, range.layerNum, aspects);
    }

    std::expected<void, std::string> Texture::transitionExpected(VriCommandBuffer*    cmd,
                                                                 VriAccessLayoutStage next,
                                                                 VriTextureViewDesc   view,
                                                                 bool                 externallySynchronized)
    {
        if (!cmd)
        {
            return std::unexpected("Texture transition requires a command buffer");
        }
        auto normalized = normalizeViewExpected(view);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        const auto available = formatAspects(desc.format);
        if (std::popcount(available) > 1 && normalized->aspect != available)
        {
            const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
            if (!caps || !caps->hasSeparateDepthStencilLayouts)
            {
                return std::unexpected("Device does not enable independent depth/stencil layouts");
            }
        }
        const VriTextureSubresourceRange range {normalized->baseMip,
                                                normalized->mipNum,
                                                normalized->baseLayer,
                                                normalized->layerNum};
        if (m_CallbackAccess && !recordCallbackAccess(cmd, next, range, normalized->aspect))
        {
            return std::unexpected(std::string(m_CallbackAccess->recording->error));
        }
        transitionRange(cmd,
                        next,
                        normalized->baseMip,
                        normalized->mipNum,
                        normalized->baseLayer,
                        normalized->layerNum,
                        normalized->aspect,
                        externallySynchronized);
        return {};
    }

    void Texture::transitionRange(VriCommandBuffer*    cmd,
                                  VriAccessLayoutStage next,
                                  uint32_t             baseMip,
                                  uint32_t             mipNum,
                                  uint32_t             baseLayer,
                                  uint32_t             layerNum,
                                  VriImageAspectFlags  aspects,
                                  bool                 externallySynchronized)
    {
        const auto textureMips   = std::max(desc.mipNum, 1u);
        const auto available     = formatAspects(desc.format);
        const auto planes        = size_t(std::popcount(available));
        // The caller supplies a memory dependency; preserve layouts without importing another queue's stages.
        const auto nativeBefore = [externallySynchronized](VriAccessLayoutStage state)
        {
            if (externallySynchronized)
            {
                state.access = VriAccess_None;
                state.stages = VriPipelineStage_AllCommands;
            }
            return state;
        };
        if (m_SubresourceStates.empty())
        {
            if (repeatedTextureRead(m_State, next))
            {
                return;
            }
            const auto layers = std::max(desc.layerNum, 1u);
            if ((mipNum != textureMips || layerNum != layers || aspects != available) && !sameState(m_State, next))
            {
                m_SubresourceStates.reserve(size_t(textureMips) * layers * planes);
            }
            const VriTextureBarrierDesc
                barrier {handle, nativeBefore(m_State), next, aspects, baseMip, mipNum, baseLayer, layerNum};
            VriBarrierGroupDesc         group {};
            group.textures   = &barrier;
            group.textureNum = 1;
            m_Device.core.CmdBarrier(cmd, &group);
        }
        else
        {
            m_Barriers.clear();
            for (uint32_t layer = baseLayer; layer < baseLayer + layerNum; ++layer)
            {
                for (uint32_t mip = baseMip; mip < baseMip + mipNum; ++mip)
                {
                    const size_t base = (size_t(layer) * textureMips + mip) * planes;
                    // Without the Vulkan feature, equal depth/stencil states must remain one combined barrier.
                    if (planes == 2 && aspects == available &&
                        sameState(m_SubresourceStates[base], m_SubresourceStates[base + 1]))
                    {
                        const auto before = m_SubresourceStates[base];
                        if (!repeatedTextureRead(before, next))
                        {
                            m_Barriers.push_back({handle, nativeBefore(before), next, available, mip, 1, layer, 1});
                        }
                        continue;
                    }
                    size_t plane = 0;
                    for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                    {
                        if (!(available & aspect))
                        {
                            continue;
                        }
                        const auto before = m_SubresourceStates[base + plane];
                        if ((aspects & aspect) && !repeatedTextureRead(before, next))
                        {
                            m_Barriers.push_back({handle, nativeBefore(before), next, aspect, mip, 1, layer, 1});
                        }
                        ++plane;
                    }
                }
            }
            if (!m_Barriers.empty())
            {
                VriBarrierGroupDesc group {};
                group.textures   = m_Barriers.data();
                group.textureNum = uint32_t(m_Barriers.size());
                m_Device.core.CmdBarrier(cmd, &group);
            }
        }
        commitState(next, {baseMip, mipNum, baseLayer, layerNum}, aspects);
    }

    void Texture::commitState(VriAccessLayoutStage next, VriTextureSubresourceRange range, VriImageAspectFlags aspects)
    {
        const auto textureMips   = std::max(desc.mipNum, 1u);
        const auto textureLayers = std::max(desc.layerNum, 1u);
        const auto available     = formatAspects(desc.format);
        const auto planes        = size_t(std::popcount(available));
        if (range.mipNum == textureMips && range.layerNum == textureLayers && aspects == available)
        {
            m_State = next;
            m_SubresourceStates.clear();
            return;
        }
        if (m_SubresourceStates.empty())
        {
            if (sameState(m_State, next))
            {
                return;
            }
            m_SubresourceStates.assign(size_t(textureMips) * textureLayers * planes, m_State);
        }
        for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerNum; ++layer)
        {
            for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipNum; ++mip)
            {
                const size_t base  = (size_t(layer) * textureMips + mip) * planes;
                size_t       plane = 0;
                for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    if (!(available & aspect))
                    {
                        continue;
                    }
                    if (aspects & aspect)
                    {
                        m_SubresourceStates[base + plane] = next;
                    }
                    ++plane;
                }
            }
        }
    }

    std::expected<void, std::string> Texture::clearStorageExpected(VriCommandBuffer*          cmd,
                                                                   const VriClearColor&       value,
                                                                   VriTextureSubresourceRange range,
                                                                   bool                       externallySynchronized)
    {
        const auto* caps     = m_Device.core.GetDeviceDesc(m_Device.handle);
        const auto  required = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferDst;
        if (!cmd || !handle || desc.sampleNum != 1 || (desc.usage & required) != required ||
            formatAspects(desc.format) != VriImageAspect_Color)
        {
            return std::unexpected("Storage clear requires a command buffer and single-sample writable color texture");
        }
        if (!caps || !caps->hasClearStorageTextureRange || !m_Device.core.CmdClearStorageTextureRange)
        {
            return std::unexpected("Device does not support selected storage texture clears");
        }
        VriTextureViewDesc view {};
        view.viewType   = (desc.type == VriTextureType_Cube || desc.type == VriTextureType_CubeArray) ?
                              VriTextureViewType_2DArray :
                              VriTextureViewType_MaxEnum;
        view.baseMip    = range.baseMip;
        view.mipNum     = range.mipNum;
        view.baseLayer  = range.baseLayer;
        view.layerNum   = range.layerNum;
        view.aspect     = VriImageAspect_Color;
        auto normalized = normalizeViewExpected(view);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        range         = {normalized->baseMip, normalized->mipNum, normalized->baseLayer, normalized->layerNum};
        auto previous = captureState();
        if (auto transitioned = transitionExpected(
                cmd,
                {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer},
                *normalized,
                externallySynchronized);
            !transitioned)
        {
            return transitioned;
        }
        const auto result = m_Device.core.CmdClearStorageTextureRange(cmd, handle, &value, &range);
        if (result != VriResult_Success)
        {
            restoreTrackedState(std::move(previous));
            if (m_CallbackAccess && m_CallbackAccess->recording->commands)
            {
                m_CallbackAccess->recording->reject(m_CallbackAccess->resource,
                                                    "texture callback failed to record a storage texture clear");
            }
            return std::unexpected("Record storage texture clear failed: " + std::to_string(result));
        }
        return {};
    }

    std::expected<void, std::string> Texture::clearDepthStencilExpected(VriCommandBuffer*           cmd,
                                                                        const VriClearDepthStencil& value,
                                                                        VriTextureSubresourceRange  range,
                                                                        VriImageAspectFlags         aspects,
                                                                        bool externallySynchronized)
    {
        const auto* caps      = m_Device.core.GetDeviceDesc(m_Device.handle);
        const auto  available = formatAspects(desc.format);
        const auto  required  = VriTextureUsage_DepthStencilAttachment | VriTextureUsage_TransferDst;
        aspects               = aspects ? aspects : available;
        if (!cmd || !handle || available == VriImageAspect_Color || (aspects & ~available) ||
            (desc.usage & required) != required ||
            ((aspects & VriImageAspect_Depth) && (!std::isfinite(value.depth) || value.depth < 0 || value.depth > 1)) ||
            ((aspects & VriImageAspect_Stencil) && value.stencil > 255))
        {
            return std::unexpected("Depth/stencil clear requires valid selected aspects, values and "
                                   "attachment/transfer-destination usage");
        }
        if (!caps || !caps->hasClearDepthStencilTextureRange || !m_Device.core.CmdClearDepthStencilTextureRange)
        {
            return std::unexpected("Native depth/stencil range clear support is not enabled");
        }
        VriTextureViewDesc view {};
        view.viewType   = (desc.type == VriTextureType_Cube || desc.type == VriTextureType_CubeArray) ?
                              VriTextureViewType_2DArray :
                              VriTextureViewType_MaxEnum;
        view.baseMip    = range.baseMip;
        view.mipNum     = range.mipNum;
        view.baseLayer  = range.baseLayer;
        view.layerNum   = range.layerNum;
        view.aspect     = aspects;
        auto normalized = normalizeViewExpected(view);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        range         = {normalized->baseMip, normalized->mipNum, normalized->baseLayer, normalized->layerNum};
        auto previous = captureState();
        if (auto transitioned = transitionExpected(
                cmd,
                {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer},
                *normalized,
                externallySynchronized);
            !transitioned)
        {
            return transitioned;
        }
        const auto result = m_Device.core.CmdClearDepthStencilTextureRange(cmd, handle, &value, &range, aspects);
        if (result != VriResult_Success)
        {
            restoreTrackedState(std::move(previous));
            if (m_CallbackAccess && m_CallbackAccess->recording->commands)
            {
                m_CallbackAccess->recording->reject(m_CallbackAccess->resource,
                                                    "texture callback failed to record a depth/stencil clear");
            }
            return std::unexpected("Record depth/stencil clear failed: " + std::to_string(result));
        }
        return {};
    }

    std::expected<VriAttachmentDesc, std::string>
    Texture::depthResolveAttachmentExpected(Texture&            target,
                                            VriAttachmentLoadOp sourceLoad,
                                            bool                recordsDraws,
                                            uint32_t            sourceLayer,
                                            uint32_t            targetMip,
                                            uint32_t            targetLayer)
    {
        if (formatAspects(desc.format) != VriImageAspect_Depth)
        {
            return std::unexpected("Depth-only resolve requires a depth-only format");
        }
        return depthStencilResolveAttachmentExpected(target,
                                                     sourceLoad,
                                                     recordsDraws,
                                                     sourceLayer,
                                                     targetMip,
                                                     targetLayer);
    }

    std::expected<VriAttachmentDesc, std::string>
    Texture::depthStencilResolveAttachmentExpected(Texture&            target,
                                                   VriAttachmentLoadOp sourceLoad,
                                                   bool                recordsDraws,
                                                   uint32_t            sourceLayer,
                                                   uint32_t            targetMip,
                                                   uint32_t            targetLayer)
    {
        const auto  aspects = formatAspects(desc.format);
        const bool  depth   = (aspects & VriImageAspect_Depth) != 0;
        const bool  stencil = (aspects & VriImageAspect_Stencil) != 0;
        const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
        if (!depth && !stencil)
        {
            return std::unexpected("Depth/stencil resolve requires a depth or stencil attachment format");
        }
        if (!caps || (depth && !caps->hasDepthAttachmentResolve) || (stencil && !caps->hasStencilAttachmentResolve))
        {
            return std::unexpected("Device does not support native resolve for the attachment's depth/stencil aspects");
        }
        if (sourceLoad != VriAttachmentLoadOp_Clear && sourceLoad != VriAttachmentLoadOp_Load &&
            sourceLoad != VriAttachmentLoadOp_DontCare)
        {
            return std::unexpected("Invalid depth/stencil resolve load action");
        }
        if (sourceLoad == VriAttachmentLoadOp_Load && !recordsDraws &&
            ((depth && !caps->hasLoadOnlyDepthAttachmentResolve) ||
             (stencil && !caps->hasLoadOnlyStencilAttachmentResolve)))
        {
            return std::unexpected("Device requires a draw in a depth/stencil resolve scope that loads its source");
        }
        if (!target.belongsTo(m_Device) || !handle || !target.handle || handle == target.handle)
        {
            return std::unexpected("Depth/stencil resolve requires two distinct textures on the same device");
        }
        const auto is2D = [](VriTextureType type)
        {
            return type == VriTextureType_2D || type == VriTextureType_2DArray;
        };
        if (target.desc.format != desc.format || !is2D(desc.type) || !is2D(target.desc.type) || desc.sampleNum <= 1 ||
            target.desc.sampleNum != 1 || !(desc.usage & VriTextureUsage_DepthStencilAttachment) ||
            !(target.desc.usage & VriTextureUsage_DepthStencilAttachment))
        {
            return std::unexpected(
                "Depth/stencil resolve requires matching 2D attachment formats and a single-sampled target");
        }
        if (sourceLayer >= desc.layerNum || targetMip >= target.desc.mipNum || targetMip >= 32 ||
            targetLayer >= target.desc.layerNum || desc.width != std::max(target.desc.width >> targetMip, 1u) ||
            desc.height != std::max(target.desc.height >> targetMip, 1u))
        {
            return std::unexpected("Depth/stencil resolve requires matching extents and valid selected mip and layers");
        }
        VriTextureViewDesc source {};
        source.viewType  = VriTextureViewType_MaxEnum;
        source.aspect    = aspects;
        source.mipNum    = 1;
        source.baseLayer = sourceLayer;
        source.layerNum  = 1;
        auto sourceView  = viewExpected(source);
        if (!sourceView)
        {
            return std::unexpected(sourceView.error());
        }
        auto destination      = source;
        destination.viewType  = VriTextureViewType_2D;
        destination.baseMip   = targetMip;
        destination.baseLayer = targetLayer;
        auto targetView       = target.viewExpected(destination);
        if (!targetView)
        {
            return std::unexpected(targetView.error());
        }
        VriAttachmentDesc attachment {};
        attachment.view        = *sourceView;
        attachment.resolveView = *targetView;
        attachment.loadOp      = sourceLoad;
        attachment.storeOp     = VriAttachmentStoreOp_Store;
        return attachment;
    }

    VriDescriptor* Texture::view()
    {
        auto result = viewExpected();
        check(result ? VriResult_Success : VriResult_Failure, result ? "Create texture view" : result.error().c_str());
        return *result;
    }

    std::expected<VriDescriptor*, std::string> Texture::viewExpected()
    {
        if (!m_View)
        {
            VriTextureViewDesc view {};
            view.viewType = VriTextureViewType_MaxEnum;
            auto result   = viewExpected(view);
            if (!result)
            {
                return result;
            }
            m_View = *result;
        }
        return m_View;
    }

    std::expected<VriTextureViewDesc, std::string> Texture::normalizeViewExpected(VriTextureViewDesc view) const
    {
        if (!handle || (view.texture && view.texture != handle))
        {
            return std::unexpected("Texture view requires this texture's native handle");
        }
        view.texture = handle;
        if (view.format == VriFormat_Unknown)
        {
            view.format = desc.format;
        }
        if (view.format != desc.format &&
            (!(desc.usage & VriTextureUsage_MutableFormat) || !m_Device.core.GetTextureViewFormatSupport ||
             !m_Device.core.GetTextureViewFormatSupport(m_Device.handle, desc.format, view.format, desc.usage)))
        {
            return std::unexpected("Texture view requires mutable creation and native format/usage compatibility");
        }
        if (view.viewType == VriTextureViewType_MaxEnum)
        {
            view.viewType = viewType(desc);
        }
        if (!view.aspect)
        {
            view.aspect = m_Aspect;
        }
        if (view.aspect & ~formatAspects(desc.format))
        {
            return std::unexpected("Texture view aspect does not belong to its format");
        }
        if (view.baseMip >= desc.mipNum || view.baseLayer >= desc.layerNum)
        {
            return std::unexpected("Texture view base mip or layer is out of range");
        }
        if (!view.mipNum)
        {
            view.mipNum = desc.mipNum - view.baseMip;
        }
        if (!view.layerNum)
        {
            view.layerNum = desc.layerNum - view.baseLayer;
        }
        if (view.mipNum > desc.mipNum - view.baseMip || view.layerNum > desc.layerNum - view.baseLayer)
        {
            return std::unexpected("Texture view mip or layer count is out of range");
        }
        if (!compatibleView(desc, view))
        {
            return std::unexpected("Texture view dimension or layer selection is incompatible with the texture");
        }
        return view;
    }

    std::expected<VriDescriptor*, std::string> Texture::viewExpected(VriTextureViewDesc view)
    {
        auto normalized = normalizeViewExpected(view);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        view              = *normalized;
        const auto cached = std::ranges::find_if(m_Views,
                                                 [&view](const CachedView& candidate)
                                                 {
                                                     const auto& key = candidate.description;
                                                     return key.viewType == view.viewType &&
                                                            key.format == view.format && key.aspect == view.aspect &&
                                                            key.baseMip == view.baseMip && key.mipNum == view.mipNum &&
                                                            key.baseLayer == view.baseLayer &&
                                                            key.layerNum == view.layerNum;
                                                 });
        if (cached != m_Views.end())
        {
            return cached->descriptor;
        }
        VriDescriptor* candidate = nullptr;
        const auto     result    = m_Device.core.CreateTextureView(m_Device.handle, &view, &candidate);
        const auto     destroy   = [this](VriDescriptor* descriptor)
        {
            m_Device.core.DestroyDescriptor(descriptor);
        };
        std::unique_ptr<VriDescriptor, decltype(destroy)> owned(candidate, destroy);
        if (result != VriResult_Success)
        {
            return std::unexpected("Create texture view failed: " + std::to_string(result));
        }
        if (!owned)
        {
            return std::unexpected("Create texture view returned no handle");
        }
        m_Views.push_back({view, owned.get()});
        return owned.release();
    }

    std::expected<VriDescriptor*, std::string> Texture::mipViewExpected(uint32_t mip)
    {
        if (mip >= desc.mipNum)
        {
            return std::unexpected("Texture mip view is out of range");
        }
        m_MipViews.resize(desc.mipNum, nullptr);
        if (!m_MipViews[mip])
        {
            VriTextureViewDesc view {};
            view.viewType = VriTextureViewType_MaxEnum;
            view.baseMip  = mip;
            view.mipNum   = 1;
            auto result   = viewExpected(view);
            if (!result)
            {
                return result;
            }
            m_MipViews[mip] = *result;
        }
        return m_MipViews[mip];
    }

    VriDescriptor* Texture::mipView(uint32_t mip)
    {
        if (mip >= desc.mipNum)
        {
            throw std::out_of_range("Texture mip view");
        }
        auto result = mipViewExpected(mip);
        check(result ? VriResult_Success : VriResult_Failure, result ? "Create mip view" : result.error().c_str());
        return *result;
    }

    Buffer::Buffer(Device& device, const VriBufferDesc& description) :
        desc(description),
        m_Device(device)
    {
        check(device.core.CreateBuffer(device.handle, &desc, &handle), "Create buffer");
    }

    Buffer::Buffer(Device& device, const VriBufferDesc& description, Uninitialized) :
        desc(description),
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<Buffer>, std::string> Buffer::create(Device& device, const VriBufferDesc& description)
    {
        auto       buffer = std::unique_ptr<Buffer>(new Buffer(device, description, Uninitialized {}));
        const auto result = device.core.CreateBuffer(device.handle, &buffer->desc, &buffer->handle);
        if (result != VriResult_Success)
            return std::unexpected("Create buffer failed: " + std::to_string(result));
        if (!buffer->handle)
            return std::unexpected("Create buffer returned no handle");
        return buffer;
    }

    Buffer::~Buffer()
    {
        if (handle)
            m_Device.core.DestroyBuffer(handle);
    }

    bool Buffer::RangeState::operator==(const RangeState& other) const
    {
        return offset == other.offset && size == other.size && sameState(access, other.access);
    }

    bool Buffer::State::belongsTo(const Buffer& buffer) const
    {
        return m_Owner == &buffer && m_Handle == buffer.handle && m_Size == buffer.desc.size;
    }

    bool Buffer::State::operator==(const State& other) const
    {
        return m_Owner == other.m_Owner && m_Handle == other.m_Handle && m_Size == other.m_Size &&
               sameState(m_Aggregate, other.m_Aggregate) && m_Ranges == other.m_Ranges;
    }

    std::expected<uint64_t, std::string> Buffer::normalizedSizeExpected(uint64_t offset, uint64_t size) const
    {
        if (!handle || offset >= desc.size || size > desc.size - offset)
        {
            return std::unexpected("Buffer byte range is empty, out of bounds or has no native handle");
        }
        return size ? size : desc.size - offset;
    }

    std::expected<VriAccessStage, std::string> Buffer::stateExpected(uint64_t offset, uint64_t size) const
    {
        auto length = normalizedSizeExpected(offset, size);
        if (!length)
        {
            return std::unexpected(length.error());
        }
        if (m_Ranges.empty())
        {
            return m_State;
        }
        VriAccessStage result {};
        bool           found = false;
        for (const auto& range : m_Ranges)
        {
            if (range.offset >= offset + *length || range.offset + range.size <= offset)
            {
                continue;
            }
            if (found && !sameState(result, range.access))
            {
                return std::unexpected("Buffer selection contains mixed access states");
            }
            result = range.access;
            found  = true;
        }
        if (!found)
        {
            return std::unexpected("Buffer range state coverage is incomplete");
        }
        return result;
    }

    std::string_view Buffer::rejectCallbackAccess(std::string_view error)
    {
        return m_CallbackAccess->recording->reject(m_CallbackAccess->resource, error);
    }

    bool Buffer::allowsCallbackAccess(VriCommandBuffer* commands, VriAccessStage access, uint64_t offset, uint64_t size)
    {
        const auto& callback = *m_CallbackAccess;
        const auto& recording = *callback.recording;
        if (!recording.commands)
        {
            return true;
        }
        if (!recording.error.empty())
        {
            return false;
        }
        const auto reject = [&](std::string_view error)
        {
            rejectCallbackAccess(error);
            return false;
        };
        if (commands != recording.commands)
        {
            return reject("buffer callback used another recording destination");
        }
        if (offset >= desc.size || size > desc.size - offset)
        {
            return reject("buffer callback range is out of bounds");
        }
        constexpr auto writeAccess = VriAccess_ShaderResourceStorageWrite | VriAccess_CopyDestinationWrite |
                                     VriAccess_AccelerationStructureWrite | VriAccess_ColorAttachmentWrite |
                                     VriAccess_DepthStencilAttachmentWrite;
        const bool writes = (access.access & writeAccess) != 0;
        const auto end = size ? offset + size : desc.size;
        auto covered = offset;
        for (const auto& range : callback.ranges)
        {
            const auto rangeEnd = range.offset + range.size;
            if (rangeEnd <= covered)
            {
                continue;
            }
            if (range.offset > covered)
            {
                break;
            }
            if (writes && !range.writable)
            {
                return reject("buffer callback requested write access to a declared read-only range");
            }
            covered = rangeEnd;
            if (covered >= end)
            {
                return true;
            }
        }
        return reject("buffer callback accessed bytes outside its declared ranges");
    }

    void Buffer::setState(VriAccessStage state)
    {
        if (m_CallbackAccess && !allowsCallbackAccess(m_CallbackAccess->recording->commands, state, 0, desc.size))
        {
            return;
        }
        m_State = state;
        m_Ranges.clear();
    }

    Buffer::State Buffer::captureState() const
    {
        State state;
        state.m_Owner     = this;
        state.m_Handle    = handle;
        state.m_Size      = desc.size;
        state.m_Aggregate = m_State;
        state.m_Ranges    = m_Ranges;
        return state;
    }

    std::expected<void, std::string> Buffer::restoreUnsubmittedExpected(State state)
    {
        if (!state.belongsTo(*this))
        {
            return std::unexpected("Buffer state snapshot belongs to another resource identity or size");
        }
        if (m_CallbackAccess && m_CallbackAccess->recording->commands)
        {
            return std::unexpected(std::string(rejectCallbackAccess("buffer callback cannot restore a discarded recording")));
        }
        restoreTrackedState(std::move(state));
        return {};
    }

    void Buffer::restoreTrackedState(State state)
    {
        m_State  = state.m_Aggregate;
        m_Ranges = std::move(state.m_Ranges);
        m_Barriers.clear();
    }

    std::expected<void, std::string>
    Buffer::restoreExpected(VriCommandBuffer* cmd, const State& state, uint64_t offset, uint64_t size)
    {
        if (!cmd || !state.belongsTo(*this))
        {
            return std::unexpected("Buffer restoration requires a command buffer and a matching state snapshot");
        }
        auto length = normalizedSizeExpected(offset, size);
        if (!length)
        {
            return std::unexpected("Restore buffer range: " + length.error());
        }
        if (m_CallbackAccess && !allowsCallbackAccess(cmd, {}, offset, *length))
        {
            return std::unexpected(std::string(m_CallbackAccess->recording->error));
        }
        auto       checkpoint = captureState();
        const auto restoreRange =
            [&](VriAccessStage access, uint64_t rangeOffset, uint64_t rangeSize) -> std::expected<void, std::string>
        {
            if (auto restored = transitionExpected(cmd, access, rangeOffset, rangeSize); !restored)
            {
                restoreTrackedState(std::move(checkpoint));
                return restored;
            }
            return {};
        };
        if (state.m_Ranges.empty())
        {
            return restoreRange(state.m_Aggregate, offset, *length);
        }
        for (const auto& range : state.m_Ranges)
        {
            const auto begin = std::max(offset, range.offset);
            const auto end   = std::min(offset + *length, range.offset + range.size);
            if (begin >= end)
            {
                continue;
            }
            if (auto restored = restoreRange(range.access, begin, end - begin); !restored)
            {
                return restored;
            }
        }
        return {};
    }

    void Buffer::prepareBarriers(VriAccessStage next, uint64_t offset, uint64_t size, bool externallySynchronized)
    {
        m_Barriers.clear();
        const auto append = [&](uint64_t begin, uint64_t length, VriAccessStage before)
        {
            if (externallySynchronized)
            {
                before = {VriAccess_None, VriPipelineStage_AllCommands};
            }
            m_Barriers.push_back({handle, before, next, begin, length});
        };
        if (!offset && size == desc.size)
        {
            append(0, 0, m_State);
            return;
        }
        // Reserve before native recording so the state commit below cannot allocate.
        m_Ranges.reserve(std::max(m_Ranges.size() + 2, size_t(3)));
        if (m_Ranges.empty())
        {
            append(offset, size, m_State);
            return;
        }
        for (const auto& range : m_Ranges)
        {
            const auto begin = std::max(offset, range.offset);
            const auto end   = std::min(offset + size, range.offset + range.size);
            if (begin < end)
            {
                append(begin, end - begin, range.access);
            }
        }
    }

    void Buffer::splitRange(uint64_t offset)
    {
        for (size_t i = 0; i < m_Ranges.size(); ++i)
        {
            auto& range = m_Ranges[i];
            if (offset > range.offset && offset < range.offset + range.size)
            {
                auto       right    = range;
                const auto leftSize = offset - range.offset;
                right.offset        = offset;
                right.size -= leftSize;
                range.size = leftSize;
                m_Ranges.insert(m_Ranges.begin() + std::ptrdiff_t(i + 1), right);
                return;
            }
        }
    }

    void Buffer::commitState(VriAccessStage next, uint64_t offset, uint64_t size)
    {
        if (!offset && size == desc.size)
        {
            m_State = next;
            m_Ranges.clear();
            return;
        }
        if (m_Ranges.empty())
        {
            m_Ranges.push_back({0, desc.size, m_State});
        }
        splitRange(offset);
        splitRange(offset + size);
        size_t retained = 0;
        for (auto range : m_Ranges)
        {
            if (range.offset >= offset && range.offset + range.size <= offset + size)
            {
                range.access = next;
            }
            if (retained && sameState(m_Ranges[retained - 1].access, range.access))
            {
                m_Ranges[retained - 1].size += range.size;
            }
            else
            {
                m_Ranges[retained++] = range;
            }
        }
        m_Ranges.resize(retained);
        if (m_Ranges.size() == 1)
        {
            m_State = m_Ranges.front().access;
            m_Ranges.clear();
            return;
        }
        m_State = {};
        for (const auto& range : m_Ranges)
        {
            m_State.access |= range.access.access;
            m_State.stages |= range.access.stages;
        }
    }

    void Buffer::transition(VriCommandBuffer* cmd, VriAccessStage next, uint64_t offset, uint64_t size)
    {
        if (m_CallbackAccess && !allowsCallbackAccess(cmd, next, offset, size))
        {
            return;
        }
        const auto length = size ? size : desc.size - offset;
        prepareBarriers(next, offset, length, false);
        VriBarrierGroupDesc group {};
        group.buffers   = m_Barriers.data();
        group.bufferNum = uint32_t(m_Barriers.size());
        m_Device.core.CmdBarrier(cmd, &group);
        commitState(next, offset, length);
    }

    std::expected<void, std::string> Buffer::transitionExpected(VriCommandBuffer* cmd,
                                                                VriAccessStage    next,
                                                                uint64_t          offset,
                                                                uint64_t          size,
                                                                bool              externallySynchronized)
    {
        auto length = normalizedSizeExpected(offset, size);
        if (!cmd || !length)
        {
            return std::unexpected(!cmd ? "Buffer transition requires a command buffer" : length.error());
        }
        if (m_CallbackAccess && !allowsCallbackAccess(cmd, next, offset, *length))
        {
            return std::unexpected(std::string(m_CallbackAccess->recording->error));
        }
        const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
        if ((offset || *length != desc.size) &&
            (!caps || !caps->hasBufferRangeBarriers || !m_Device.core.CmdBufferBarrierRange))
        {
            return std::unexpected("Native buffer byte-range barriers are not enabled");
        }
        prepareBarriers(next, offset, *length, externallySynchronized);
        if (caps && caps->hasBufferRangeBarriers && m_Device.core.CmdBufferBarrierRange)
        {
            for (const auto& barrier : m_Barriers)
            {
                const auto status = m_Device.core.CmdBufferBarrierRange(cmd, &barrier);
                if (status != VriResult_Success)
                {
                    return std::unexpected("Record buffer barrier: " + std::to_string(status) +
                                           "; discard unsubmitted commands");
                }
            }
        }
        else
        {
            // The existing whole-buffer command remains valid on backends without byte-range support.
            VriBarrierGroupDesc group {};
            group.buffers   = m_Barriers.data();
            group.bufferNum = uint32_t(m_Barriers.size());
            m_Device.core.CmdBarrier(cmd, &group);
        }
        commitState(next, offset, *length);
        return {};
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
