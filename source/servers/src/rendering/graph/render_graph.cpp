#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>
#include <ranges>

namespace vultra
{
    namespace
    {
        class GraphStateScope
        {
        public:
            explicit GraphStateScope(bool& active) :
                m_Active(active),
                m_Previous(std::exchange(active, true))
            {
            }

            ~GraphStateScope()
            {
                m_Active = m_Previous;
            }

            GraphStateScope(const GraphStateScope&)            = delete;
            GraphStateScope& operator=(const GraphStateScope&) = delete;

        private:
            bool& m_Active;
            bool  m_Previous;
        };

        VriImageAspectFlags textureAspects(VriFormat format)
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

        // History uses the same layer/mip/aspect cell order as pass dependency and texture-state tracking.
        void visitTextureCells(const VriTextureDesc& desc, RenderGraph::TextureRange range, const auto& visit)
        {
            const auto available = textureAspects(desc.format);
            const auto planes    = size_t(std::popcount(available));
            for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerNum; ++layer)
            {
                for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipNum; ++mip)
                {
                    size_t plane = 0;
                    for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                    {
                        if (available & aspect)
                        {
                            if (range.aspect & aspect)
                            {
                                visit((size_t(layer) * desc.mipNum + mip) * planes + plane);
                            }
                            ++plane;
                        }
                    }
                }
            }
        }

        RenderGraph::TextureRange textureCellRange(const VriTextureDesc& desc, size_t cell)
        {
            const auto available = textureAspects(desc.format);
            const auto planes    = size_t(std::popcount(available));
            auto       aspect    = available;
            if (planes > 1)
            {
                aspect = cell % planes ? VriImageAspect_Stencil : VriImageAspect_Depth;
            }
            return {uint32_t((cell / planes) % desc.mipNum), 1, uint32_t(cell / (planes * desc.mipNum)), 1, aspect};
        }

        void visitBufferCells(std::span<const uint64_t> boundaries, RenderGraph::BufferRange range, const auto& visit)
        {
            for (size_t cell = 0; cell + 1 < boundaries.size(); ++cell)
            {
                if (boundaries[cell] < range.offset + range.size && range.offset < boundaries[cell + 1])
                {
                    visit(cell);
                }
            }
        }

        bool overlaps(RenderGraph::BufferRange a, RenderGraph::BufferRange b)
        {
            return a.offset < b.offset + b.size && b.offset < a.offset + a.size;
        }

        bool overlaps(RenderGraph::TextureRange a, RenderGraph::TextureRange b)
        {
            return (a.aspect & b.aspect) && a.baseMip < b.baseMip + b.mipNum && b.baseMip < a.baseMip + a.mipNum &&
                   a.baseLayer < b.baseLayer + b.layerNum && b.baseLayer < a.baseLayer + a.layerNum;
        }

        bool covers(RenderGraph::BufferRange outer, RenderGraph::BufferRange inner)
        {
            return outer.offset <= inner.offset && inner.offset + inner.size <= outer.offset + outer.size;
        }

        bool covers(RenderGraph::TextureRange outer, RenderGraph::TextureRange inner)
        {
            return (outer.aspect & inner.aspect) == inner.aspect && outer.baseMip <= inner.baseMip &&
                   inner.baseMip + inner.mipNum <= outer.baseMip + outer.mipNum && outer.baseLayer <= inner.baseLayer &&
                   inner.baseLayer + inner.layerNum <= outer.baseLayer + outer.layerNum;
        }

        bool coversRanges(const VriTextureDesc* texture, const RenderGraph::ResourceRange& selection, auto& ranges)
        {
            if (!texture)
            {
                const auto end = selection.bufferRange.offset + selection.bufferRange.size;
                for (auto covered = selection.bufferRange.offset; covered < end;)
                {
                    auto next = covered;
                    for (const auto& candidate : ranges)
                    {
                        const auto range = candidate.bufferRange;
                        if (range.offset <= covered)
                        {
                            next = std::max(next, range.offset + range.size);
                        }
                    }
                    if (next == covered)
                    {
                        return false;
                    }
                    covered = next;
                }
                return true;
            }

            bool covered = true;
            visitTextureCells(*texture,
                              selection.range,
                              [&](size_t index)
                              {
                                  const auto cell = textureCellRange(*texture, index);
                                  covered         = covered && std::ranges::any_of(ranges,
                                                                           [cell](const auto& candidate)
                                                                           {
                                                                               return covers(candidate.range, cell);
                                                                           });
                              });
            return covered;
        }

        void resetBufferHistory(std::vector<RenderGraph::BufferHistoryRange>& history,
                                RenderGraph::BufferRange                      selection,
                                uint64_t                                      epoch)
        {
            std::vector<RenderGraph::BufferHistoryRange> result;
            const auto append = [&](RenderGraph::BufferRange range, RenderGraph::HistoryInfo state)
            {
                if (!range.size)
                {
                    return;
                }
                if (!result.empty() && result.back().state.epoch == state.epoch &&
                    result.back().state.resetPending == state.resetPending)
                {
                    result.back().range.size += range.size;
                }
                else
                {
                    result.push_back({range, state});
                }
            };
            for (const auto& current : history)
            {
                const auto begin = std::max(current.range.offset, selection.offset);
                const auto end = std::min(current.range.offset + current.range.size, selection.offset + selection.size);
                if (begin >= end)
                {
                    append(current.range, current.state);
                    continue;
                }
                append({current.range.offset, begin - current.range.offset}, current.state);
                append({begin, end - begin}, {epoch, true});
                append({end, current.range.offset + current.range.size - end}, current.state);
            }
            history = std::move(result);
        }

        std::expected<void, std::string> validateTextureExpected(const VriTextureDesc& desc, Device& device)
        {
            const auto* caps = device.core.GetDeviceDesc(device.handle);
            if (!caps || !desc.width || !desc.height || !desc.depth || !desc.mipNum || !desc.layerNum ||
                !desc.sampleNum)
            {
                return std::unexpected("Graph texture dimensions/counts and device limits must be available");
            }
            if (desc.layerNum > std::max(caps->textureArrayLayerMaxNum, 1u) ||
                desc.mipNum > std::bit_width(std::max({desc.width, desc.height, desc.depth})))
            {
                return std::unexpected("Graph texture mip/layer counts exceed its dimensions or device limits");
            }
            uint32_t limit = 0;
            switch (desc.type)
            {
                case VriTextureType_1D:
                case VriTextureType_1DArray:
                    limit = caps->texture1DMaxDim;
                    if (desc.height != 1 || desc.depth != 1 || desc.sampleNum != 1)
                    {
                        return std::unexpected("Graph 1D textures require unit height/depth and no MSAA");
                    }
                    break;
                case VriTextureType_2D:
                case VriTextureType_2DArray:
                    limit = caps->texture2DMaxDim;
                    if (desc.depth != 1)
                    {
                        return std::unexpected("Graph 2D textures require unit depth");
                    }
                    break;
                case VriTextureType_3D:
                    limit = caps->texture3DMaxDim;
                    if (desc.layerNum != 1 || desc.sampleNum != 1)
                    {
                        return std::unexpected("Graph 3D textures require one layer and no MSAA");
                    }
                    break;
                case VriTextureType_Cube:
                case VriTextureType_CubeArray:
                    limit = caps->texture2DMaxDim;
                    if (desc.width != desc.height || desc.depth != 1 || desc.layerNum % 6 || desc.sampleNum != 1)
                    {
                        return std::unexpected(
                            "Graph cube textures require square faces, groups of six layers and no MSAA");
                    }
                    break;
                default:
                    return std::unexpected("Unknown graph texture dimension");
            }
            if (!limit || std::max({desc.width, desc.height, desc.depth}) > limit ||
                (desc.sampleNum != 1 && desc.mipNum != 1))
            {
                return std::unexpected("Graph texture dimensions/MSAA mip count exceed the native contract");
            }
            const bool depthStencil = textureAspects(desc.format) != VriImageAspect_Color;
            if ((desc.usage & VriTextureUsage_DepthStencilAttachment) && !depthStencil)
            {
                return std::unexpected("Graph depth/stencil attachments require a depth/stencil format");
            }
            if (depthStencil &&
                (desc.usage & (VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResourceStorage)))
            {
                return std::unexpected("Depth/stencil textures cannot be color or storage attachments");
            }
            return {};
        }

        void validateTexture(const VriTextureDesc& desc, Device& device)
        {
            if (auto valid = validateTextureExpected(desc, device); !valid)
            {
                throw std::invalid_argument(valid.error());
            }
        }

        std::expected<RenderGraph::TextureRange, std::string> normalizedRangeExpected(RenderGraph::TextureRange range,
                                                                                      const VriTextureDesc*     desc)
        {
            if (!desc)
            {
                if (range.baseMip || range.mipNum || range.baseLayer || range.layerNum || range.aspect)
                {
                    return std::unexpected("Buffers do not select texture mip/layer ranges");
                }
                return RenderGraph::TextureRange {0, 1, 0, 1};
            }
            const auto available = textureAspects(desc->format);
            if (!range.aspect)
            {
                range.aspect = available;
            }
            if (range.aspect & ~available)
            {
                return std::unexpected("Graph use aspect does not belong to its texture format");
            }
            if (range.baseMip >= desc->mipNum || range.baseLayer >= desc->layerNum)
            {
                return std::unexpected("Graph use base mip/layer is out of range");
            }
            if (!range.mipNum)
            {
                range.mipNum = desc->mipNum - range.baseMip;
            }
            if (!range.layerNum)
            {
                range.layerNum = desc->layerNum - range.baseLayer;
            }
            if (range.mipNum > desc->mipNum - range.baseMip || range.layerNum > desc->layerNum - range.baseLayer)
            {
                return std::unexpected("Graph use mip/layer count is out of range");
            }
            return range;
        }

        std::expected<RenderGraph::BufferRange, std::string>
        normalizedBufferRangeExpected(RenderGraph::BufferRange range, const VriBufferDesc* desc)
        {
            if (!desc)
            {
                if (range.offset || range.size)
                {
                    return std::unexpected("Buffer byte coordinates supplied for a texture");
                }
                return RenderGraph::BufferRange {};
            }
            if (range.offset >= desc->size || range.size > desc->size - range.offset)
            {
                return std::unexpected("Buffer byte range is empty or out of bounds");
            }
            if (!range.size)
            {
                range.size = desc->size - range.offset;
            }
            return range;
        }

        VriTextureViewDesc transitionView(const Texture& texture, RenderGraph::TextureRange range)
        {
            VriTextureViewDesc view {};
            // Cube faces are independent cells, not necessarily a complete cube view.
            view.viewType =
                (texture.desc.type == VriTextureType_Cube || texture.desc.type == VriTextureType_CubeArray) ?
                    VriTextureViewType_2DArray :
                    VriTextureViewType_MaxEnum;
            view.aspect    = range.aspect ? range.aspect : textureAspects(texture.desc.format);
            view.baseMip   = range.baseMip;
            view.mipNum    = range.mipNum;
            view.baseLayer = range.baseLayer;
            view.layerNum  = range.layerNum;
            return view;
        }

        std::expected<uint32_t, std::string> requiredUsageExpected(Usage usage, bool texture)
        {
            switch (usage)
            {
                case Usage::eCopySource:
                    return texture ? uint32_t(VriTextureUsage_TransferSrc) : uint32_t(VriBufferUsage_TransferSrc);
                case Usage::eCopyDestination:
                    return texture ? uint32_t(VriTextureUsage_TransferDst) : uint32_t(VriBufferUsage_TransferDst);
                case Usage::eStorageRead:
                case Usage::eStorageWrite:
                case Usage::eStorageReadWrite:
                    return texture ? uint32_t(VriTextureUsage_ShaderResourceStorage) :
                                     uint32_t(VriBufferUsage_StorageBuffer);
                case Usage::eColorWrite:
                case Usage::eColorReadWrite:
                    if (texture)
                    {
                        return VriTextureUsage_ColorAttachment;
                    }
                    break;
                case Usage::eDepthWrite:
                case Usage::eDepthRead:
                case Usage::eDepthReadOnly:
                case Usage::eDepthReadWrite:
                case Usage::eDepthWriteResolve:
                case Usage::eDepthReadWriteResolve:
                case Usage::eDepthResolveDestination:
                    if (texture)
                    {
                        return VriTextureUsage_DepthStencilAttachment;
                    }
                    break;
                case Usage::eSampled:
                    if (texture)
                    {
                        return VriTextureUsage_ShaderResource;
                    }
                    break;
                case Usage::ePresent:
                    if (texture)
                    {
                        return 0;
                    }
                    break;
                case Usage::eIndirect:
                    if (!texture)
                    {
                        return VriBufferUsage_IndirectBuffer;
                    }
                    break;
                case Usage::eConstantRead:
                    if (!texture)
                    {
                        return VriBufferUsage_ConstantBuffer;
                    }
                    break;
            }
            return std::unexpected("Unsupported graph usage for this resource type");
        }

        bool reads(Usage u)
        {
            return u != Usage::eColorWrite && u != Usage::eDepthWrite && u != Usage::eStorageWrite &&
                   u != Usage::eCopyDestination && u != Usage::eDepthWriteResolve &&
                   u != Usage::eDepthResolveDestination;
        }

        bool writes(Usage u)
        {
            return u == Usage::eColorWrite || u == Usage::eColorReadWrite || u == Usage::eDepthWrite ||
                   u == Usage::eDepthReadWrite || u == Usage::eStorageWrite || u == Usage::eStorageReadWrite ||
                   u == Usage::eCopyDestination || u == Usage::eDepthWriteResolve ||
                   u == Usage::eDepthReadWriteResolve || u == Usage::eDepthResolveDestination;
        }

        VriAccessLayoutStage stateForUsage(Usage u)
        {
            switch (u)
            {
                case Usage::eColorWrite:
                    return {VriAccess_ColorAttachmentWrite,
                            VriLayout_ColorAttachment,
                            VriPipelineStage_ColorAttachmentOutput};
                case Usage::eColorReadWrite:
                    return {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                            VriLayout_ColorAttachment,
                            VriPipelineStage_ColorAttachmentOutput};
                case Usage::eSampled:
                    return {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands};
                case Usage::eDepthWrite:
                    return {VriAccess_DepthStencilAttachmentWrite,
                            VriLayout_DepthStencilAttachment,
                            VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests};
                case Usage::eDepthReadOnly:
                    return {VriAccess_DepthStencilAttachmentRead,
                            VriLayout_DepthStencilReadOnly,
                            VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests};
                case Usage::eDepthRead:
                    // VRI stores bound depth even with pipeline writes disabled. Preserve read-only dataflow
                    // for pass culling, but synchronize that attachment store at execution.
                case Usage::eDepthReadWrite:
                    return {VriAccess_DepthStencilAttachmentRead | VriAccess_DepthStencilAttachmentWrite,
                            VriLayout_DepthStencilAttachment,
                            VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests};
                case Usage::eDepthWriteResolve:
                case Usage::eDepthReadWriteResolve: {
                    const VriAccessFlags load =
                        u == Usage::eDepthReadWriteResolve ? VriAccess_DepthStencilAttachmentRead : VriAccess_None;
                    return {load | VriAccess_DepthStencilAttachmentWrite | VriAccess_ColorAttachmentRead,
                            VriLayout_DepthStencilAttachment,
                            VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests |
                                VriPipelineStage_ColorAttachmentOutput};
                }
                case Usage::eDepthResolveDestination:
                    // Vulkan scopes fixed-function depth resolves as color output accesses.
                    return {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                            VriLayout_DepthStencilAttachment,
                            VriPipelineStage_ColorAttachmentOutput};
                case Usage::eStorageRead:
                    return {VriAccess_ShaderResourceStorageRead, VriLayout_General, VriPipelineStage_AllCommands};
                case Usage::eStorageWrite:
                    return {VriAccess_ShaderResourceStorageWrite, VriLayout_General, VriPipelineStage_AllCommands};
                case Usage::eStorageReadWrite:
                    return {VriAccess_ShaderResourceStorageRead | VriAccess_ShaderResourceStorageWrite,
                            VriLayout_General,
                            VriPipelineStage_AllCommands};
                case Usage::eCopySource:
                    return {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer};
                case Usage::eCopyDestination:
                    return {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer};
                case Usage::ePresent:
                    return {VriAccess_None, VriLayout_Present, VriPipelineStage_AllCommands};
                case Usage::eIndirect:
                    return {VriAccess_IndirectBufferRead, VriLayout_Undefined, VriPipelineStage_DrawIndirect};
                case Usage::eConstantRead:
                    return {VriAccess_ConstantBufferRead, VriLayout_Undefined, VriPipelineStage_AllCommands};
            }
            throw std::logic_error("Unknown graph usage");
        }
    } // namespace

    const char* usageName(Usage usage)
    {
        switch (usage)
        {
            case Usage::eColorWrite:
                return "color write";
            case Usage::eColorReadWrite:
                return "color read/write";
            case Usage::eDepthWrite:
                return "depth write";
            case Usage::eDepthRead:
                return "depth read";
            case Usage::eDepthReadOnly:
                return "read-only depth attachment";
            case Usage::eDepthReadWrite:
                return "depth read/write";
            case Usage::eDepthWriteResolve:
                return "depth write and resolve";
            case Usage::eDepthReadWriteResolve:
                return "depth read/write and resolve";
            case Usage::eDepthResolveDestination:
                return "depth resolve destination";
            case Usage::eSampled:
                return "sampled";
            case Usage::eStorageRead:
                return "storage read";
            case Usage::eStorageWrite:
                return "storage write";
            case Usage::eStorageReadWrite:
                return "storage read/write";
            case Usage::eCopySource:
                return "copy source";
            case Usage::eCopyDestination:
                return "copy destination";
            case Usage::ePresent:
                return "present";
            case Usage::eIndirect:
                return "indirect buffer";
            case Usage::eConstantRead:
                return "constant buffer read";
        }
        throw std::logic_error("Unknown graph usage");
    }

    RenderGraph::AppendTransaction::AppendTransaction(RenderGraph& graph) :
        m_Graph(graph),
        m_Parent(std::exchange(graph.m_ActiveAppend, this)),
        m_Resources(graph.m_Resources.size()),
        m_Passes(graph.m_Passes.size()),
        m_Exports(graph.m_Exports.size()),
        m_Compiled(graph.m_Compiled)
    {
    }

    RenderGraph::AppendTransaction::~AppendTransaction()
    {
        // Destroy callbacks and publish native labels without allowing same-graph reentry.
        const GraphStateScope finalizing(m_Graph.m_StateTransactionActive);
        m_Graph.m_ActiveAppend = m_Parent;
        if (!m_Committed)
        {
            m_Graph.m_Passes.resize(m_Passes);
            m_Graph.m_Exports.resize(m_Exports);
            m_Graph.m_Resources.resize(m_Resources);
            m_Graph.m_Compiled = m_Compiled;
        }
        else if (!m_Parent)
        {
            for (const auto& entry : std::span(m_Graph.m_Resources).subspan(m_Resources))
            {
                if (entry.imported)
                {
                    m_Graph.labelImport(entry);
                }
            }
        }
    }

    std::expected<void, std::string> RenderGraph::checkStateTransactionIdleExpected() const
    {
        if (m_StateTransactionActive)
        {
            return std::unexpected("RenderGraph has an active state transaction");
        }
        return {};
    }

    std::expected<void, std::string> RenderGraph::checkGraphIdleExpected() const
    {
        if (auto idle = checkStateTransactionIdleExpected(); !idle)
        {
            return idle;
        }
        if (m_ActiveAppend)
        {
            return std::unexpected("RenderGraph cannot change or record state during graph construction");
        }
        return {};
    }

    std::expected<void, std::string> RenderGraph::checkGraphEditExpected() const
    {
        if (auto idle = checkStateTransactionIdleExpected(); !idle)
        {
            return idle;
        }
        if (m_Aliased)
        {
            return std::unexpected("Rebuild the graph to change an aliased resource plan");
        }
        return {};
    }

    void RenderGraph::edit()
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            throw std::logic_error(editable.error()); // Legacy boundary.
        }
    }

    RenderGraph::Entry& RenderGraph::lookup(Resource resource)
    {
        if (resource.graph != this || resource.index >= m_Resources.size())
        {
            throw std::invalid_argument("Resource belongs to another graph");
        }
        return m_Resources[resource.index];
    }

    void RenderGraph::labelImport(const Entry& entry) const
    {
        if (!m_ActiveAppend)
        {
            m_Device.core.SetDebugName(entry.isTexture ? static_cast<void*>(entry.texture->handle) :
                                                         static_cast<void*>(entry.buffer->handle),
                                       entry.name.c_str());
        }
    }

    RenderGraph::Resource RenderGraph::importResource(std::string name, Texture& texture, bool initialized)
    {
        edit();
        validateTexture(texture.desc, m_Device);
        for (const auto& entry : m_Resources)
        {
            if (entry.texture && entry.texture->handle == texture.handle)
            {
                throw std::invalid_argument("Import each texture only once");
            }
        }
        Entry entry;
        entry.name        = std::move(name);
        entry.imported    = true;
        entry.initialized = initialized;
        entry.texture     = &texture;
        entry.textureDesc = texture.desc;
        labelImport(entry);
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return {this, uint32_t(m_Resources.size() - 1)};
    }

    RenderGraph::Resource RenderGraph::importResource(std::string name, Buffer& buffer, bool initialized)
    {
        edit();
        for (const auto& entry : m_Resources)
        {
            if (entry.buffer && entry.buffer->handle == buffer.handle)
            {
                throw std::invalid_argument("Import each buffer only once");
            }
        }
        Entry entry;
        entry.name        = std::move(name);
        entry.isTexture   = false;
        entry.imported    = true;
        entry.initialized = initialized;
        entry.buffer      = &buffer;
        entry.bufferDesc  = buffer.desc;
        labelImport(entry);
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return {this, uint32_t(m_Resources.size() - 1)};
    }

    std::expected<RenderGraph::Resource, std::string>
    RenderGraph::importResourceExpected(std::string name, Texture& texture, bool initialized, ImportOwnership ownership)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (!texture.belongsTo(m_Device) || !texture.handle)
        {
            return std::unexpected("Invalid graph texture device or handle");
        }
        if (auto valid = validateTextureExpected(texture.desc, m_Device); !valid)
        {
            return std::unexpected(valid.error());
        }
        if (m_Resources.size() >= UINT32_MAX)
        {
            return std::unexpected("Too many graph resources");
        }
        for (const auto& entry : m_Resources)
        {
            if (entry.texture && entry.texture->handle == texture.handle)
            {
                return std::unexpected("Import each texture only once");
            }
        }
        Entry entry;
        entry.name        = std::move(name);
        entry.imported    = true;
        entry.importOwnership = ownership;
        entry.initialized = initialized;
        entry.texture     = &texture;
        entry.textureDesc = texture.desc;
        labelImport(entry);
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return Resource {this, uint32_t(m_Resources.size() - 1)};
    }

    std::expected<RenderGraph::Resource, std::string>
    RenderGraph::importResourceExpected(std::string name, Buffer& buffer, bool initialized, ImportOwnership ownership)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (!buffer.belongsTo(m_Device) || !buffer.handle)
        {
            return std::unexpected("Invalid graph buffer device or handle");
        }
        if (m_Resources.size() >= UINT32_MAX)
        {
            return std::unexpected("Too many graph resources");
        }
        for (const auto& entry : m_Resources)
        {
            if (entry.buffer && entry.buffer->handle == buffer.handle)
            {
                return std::unexpected("Import each buffer only once");
            }
        }
        Entry entry;
        entry.name        = std::move(name);
        entry.isTexture   = false;
        entry.imported    = true;
        entry.importOwnership = ownership;
        entry.initialized = initialized;
        entry.buffer      = &buffer;
        entry.bufferDesc  = buffer.desc;
        labelImport(entry);
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return Resource {this, uint32_t(m_Resources.size() - 1)};
    }

    RenderGraph::Resource RenderGraph::createTexture(std::string name, const VriTextureDesc& desc)
    {
        edit();
        validateTexture(desc, m_Device);
        Entry entry;
        entry.name        = std::move(name);
        entry.textureDesc = desc;
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return {this, uint32_t(m_Resources.size() - 1)};
    }

    std::expected<RenderGraph::Resource, std::string> RenderGraph::createTextureExpected(std::string           name,
                                                                                         const VriTextureDesc& desc)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (auto valid = validateTextureExpected(desc, m_Device); !valid)
        {
            return std::unexpected(valid.error());
        }
        if (m_Resources.size() >= UINT32_MAX)
        {
            return std::unexpected("Too many graph resources");
        }
        Entry entry;
        entry.name        = std::move(name);
        entry.textureDesc = desc;
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return Resource {this, uint32_t(m_Resources.size() - 1)};
    }

    RenderGraph::Resource RenderGraph::createBuffer(std::string name, const VriBufferDesc& desc)
    {
        edit();
        Entry entry;
        entry.name       = std::move(name);
        entry.isTexture  = false;
        entry.bufferDesc = desc;
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return {this, uint32_t(m_Resources.size() - 1)};
    }

    std::expected<RenderGraph::Resource, std::string> RenderGraph::createBufferExpected(std::string          name,
                                                                                        const VriBufferDesc& desc)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (m_Resources.size() >= UINT32_MAX)
        {
            return std::unexpected("Too many graph resources");
        }
        Entry entry;
        entry.name       = std::move(name);
        entry.isTexture  = false;
        entry.bufferDesc = desc;
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return Resource {this, uint32_t(m_Resources.size() - 1)};
    }

    void RenderGraph::addPass(std::string                name,
                              std::initializer_list<Use> uses,
                              ExecutePass                execute,
                              bool                       sideEffect,
                              CapturePassState           captureState)
    {
        addPass(std::move(name),
                std::span(uses.begin(), uses.size()),
                std::move(execute),
                sideEffect,
                std::move(captureState));
    }

    void RenderGraph::addPass(std::string          name,
                              std::span<const Use> uses,
                              ExecutePass          execute,
                              bool                 sideEffect,
                              CapturePassState     captureState)
    {
        edit();
        if (!execute)
        {
            throw std::invalid_argument("Pass needs an execute function");
        }
        for (const auto& use : uses)
        {
            lookup(use.resource);
        }
        m_Passes.push_back({std::move(name), {uses.begin(), uses.end()}, std::move(execute), sideEffect});
        m_Passes.back().captureState = std::move(captureState);
        m_Compiled                   = false;
    }

    std::expected<void, std::string> RenderGraph::addPassExpected(std::string          name,
                                                                  std::span<const Use> uses,
                                                                  ExecutePassExpected  execute,
                                                                  bool                 sideEffect,
                                                                  CapturePassState     captureState)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (!execute)
        {
            return std::unexpected("Pass needs an execute function");
        }
        for (const auto& use : uses)
        {
            if (use.resource.graph != this || use.resource.index >= m_Resources.size())
            {
                return std::unexpected("Resource belongs to another graph");
            }
        }
        Pass pass;
        pass.name = std::move(name);
        pass.uses.assign(uses.begin(), uses.end());
        pass.sideEffect      = sideEffect;
        pass.executeExpected = std::move(execute);
        pass.captureState    = std::move(captureState);
        m_Passes.push_back(std::move(pass));
        m_Compiled = false;
        return {};
    }

    void RenderGraph::nameResource(Resource resource, std::string name)
    {
        if (auto named = nameResourceExpected(resource, std::move(name)); !named)
        {
            throw std::invalid_argument(named.error()); // Legacy boundary.
        }
    }

    std::expected<void, std::string> RenderGraph::nameResourceExpected(Resource resource, std::string name)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (!ownsResource(resource))
        {
            return std::unexpected("Resource belongs to another graph");
        }
        if (name.empty())
        {
            return std::unexpected("RenderGraph resource name is empty");
        }
        if (m_ActiveAppend && resource.index < m_ActiveAppend->m_Resources)
        {
            return std::unexpected("Cannot rename a resource that predates this graph construction");
        }
        m_Resources[resource.index].name = std::move(name);
        return {};
    }

    void RenderGraph::preserveContents(Resource resource)
    {
        if (auto preserved = preserveContentsExpected(resource); !preserved)
        {
            throw std::invalid_argument(preserved.error()); // Legacy boundary.
        }
    }

    std::expected<void, std::string> RenderGraph::preserveContentsExpected(Resource resource)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return editable;
        }
        if (!ownsResource(resource) || (m_ActiveAppend && resource.index < m_ActiveAppend->m_Resources))
        {
            return std::unexpected("Cannot preserve contents of a foreign resource or an append prefix");
        }
        m_Resources[resource.index].preserve = true;
        return {};
    }

    void RenderGraph::exportResource(Resource resource)
    {
        if (auto exported = exportResourceExpected(resource); !exported)
        {
            throw std::invalid_argument(exported.error()); // Legacy boundary.
        }
    }

    std::expected<void, std::string> RenderGraph::exportResourceExpected(Resource resource)
    {
        return exportRangeExpected({resource});
    }

    std::expected<void, std::string> RenderGraph::exportRangeExpected(ResourceRange selection)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (!ownsResource(selection.resource))
        {
            return std::unexpected("Resource belongs to another graph");
        }
        const auto& entry   = m_Resources[selection.resource.index];
        auto        texture = normalizedRangeExpected(selection.range, entry.isTexture ? &entry.textureDesc : nullptr);
        auto        buffer =
            normalizedBufferRangeExpected(selection.bufferRange, entry.isTexture ? nullptr : &entry.bufferDesc);
        if (!texture || !buffer)
        {
            return std::unexpected(texture ? buffer.error() : texture.error());
        }
        m_Exports.push_back({selection.resource.index, entry.isTexture ? *texture : TextureRange {}, *buffer});
        m_Compiled = false;
        return {};
    }

    bool RenderGraph::isExported(size_t index) const
    {
        return std::ranges::any_of(m_Exports,
                                   [index](const auto& selection)
                                   {
                                       return selection.resourceIndex == index;
                                   });
    }

    bool RenderGraph::coversExportedRange(ResourceRange normalized) const
    {
        const auto index       = normalized.resource.index;
        const auto forResource = [index](const ExportInfo& exported)
        {
            return exported.resourceIndex == index;
        };
        auto        exports = m_Exports | std::views::filter(forResource);
        const auto& entry   = m_Resources[index];
        return coversRanges(entry.isTexture ? &entry.textureDesc : nullptr, normalized, exports);
    }

    RenderGraph::Resource RenderGraph::findResource(std::string_view name) const
    {
        auto resource = findResourceExpected(name);
        if (!resource)
        {
            throw std::invalid_argument(resource.error());
        }
        return *resource;
    }

    std::expected<RenderGraph::Resource, std::string> RenderGraph::findResourceExpected(std::string_view name) const
    {
        uint32_t found = UINT32_MAX;
        for (uint32_t i = 0; i < m_Resources.size(); ++i)
        {
            if (m_Resources[i].name == name)
            {
                if (found != UINT32_MAX)
                {
                    return std::unexpected("Ambiguous graph resource: " + std::string(name));
                }
                found = i;
            }
        }
        if (found == UINT32_MAX)
        {
            return std::unexpected("Unknown graph resource: " + std::string(name));
        }
        return Resource {this, found};
    }

    RenderGraph::Resource RenderGraph::captureAfterPass(const std::string& passName, Resource source, std::string name)
    {
        auto captured = captureAfterPassExpected(passName, source, std::move(name));
        if (!captured)
        {
            throw std::invalid_argument(captured.error()); // Legacy boundary.
        }
        return *captured;
    }

    std::expected<RenderGraph::Resource, std::string>
    RenderGraph::captureAfterPassExpected(const std::string& passName, Resource source, std::string name)
    {
        return captureRangeAfterPassExpected(passName, ResourceRange {source}, std::move(name));
    }

    std::expected<RenderGraph::Resource, std::string> RenderGraph::captureAfterPassExpected(const std::string& passName,
                                                                                            Resource           source,
                                                                                            std::string        name,
                                                                                            BufferRange        range)
    {
        return captureRangeAfterPassExpected(passName, ResourceRange {source, {}, range}, std::move(name));
    }

    std::expected<RenderGraph::Resource, std::string>
    RenderGraph::captureRangeAfterPassExpected(const std::string& passName, ResourceRange selection, std::string name)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        const auto source = selection.resource;
        if (!ownsResource(source))
        {
            return std::unexpected("Resource belongs to another graph");
        }
        if (name.empty())
        {
            return std::unexpected("Captured resource needs a nonempty name");
        }
        const auto& entry = m_Resources[source.index];
        const bool  isTexture = entry.isTexture;
        auto        texture   = normalizedRangeExpected(selection.range, isTexture ? &entry.textureDesc : nullptr);
        auto buffer = normalizedBufferRangeExpected(selection.bufferRange, isTexture ? nullptr : &entry.bufferDesc);
        if (!texture || !buffer)
        {
            return std::unexpected(texture ? buffer.error() : texture.error());
        }
        const auto usage = isTexture ? entry.textureDesc.usage : entry.bufferDesc.usage;
        if (!(usage & (isTexture ? uint32_t(VriTextureUsage_TransferSrc) : uint32_t(VriBufferUsage_TransferSrc))))
        {
            return std::unexpected("Capture requires TransferSrc usage");
        }
        auto         textureDesc = entry.textureDesc;
        TextureRange destination {};
        if (isTexture)
        {
            const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
            if (texture->aspect != textureAspects(textureDesc.format) &&
                (!caps || !caps->hasSeparateDepthStencilLayouts))
            {
                return std::unexpected("Texture capture requires independent depth/stencil layouts for one aspect");
            }
            textureDesc.width    = std::max(textureDesc.width >> texture->baseMip, 1u);
            textureDesc.height   = std::max(textureDesc.height >> texture->baseMip, 1u);
            textureDesc.depth    = std::max(textureDesc.depth >> texture->baseMip, 1u);
            textureDesc.mipNum   = texture->mipNum;
            textureDesc.layerNum = texture->layerNum;
            if ((textureDesc.type == VriTextureType_Cube || textureDesc.type == VriTextureType_CubeArray) &&
                (texture->baseLayer % 6 || texture->layerNum % 6))
            {
                textureDesc.type = texture->layerNum == 1 ? VriTextureType_2D : VriTextureType_2DArray;
            }
            textureDesc.usage |= VriTextureUsage_TransferDst;
            destination.aspect = texture->aspect;
        }
        const auto writesSource = [&](const Pass& pass)
        {
            return std::ranges::any_of(pass.uses,
                                       [&](const Use& use)
                                       {
                                           if (use.resource.index != source.index || !writes(use.usage))
                                           {
                                               return false;
                                           }
                                           if (isTexture)
                                           {
                                               auto written = normalizedRangeExpected(use.range, &entry.textureDesc);
                                               return written && (written->aspect & texture->aspect) &&
                                                      written->baseMip < texture->baseMip + texture->mipNum &&
                                                      texture->baseMip < written->baseMip + written->mipNum &&
                                                      written->baseLayer < texture->baseLayer + texture->layerNum &&
                                                      texture->baseLayer < written->baseLayer + written->layerNum;
                                           }
                                           auto written =
                                               normalizedBufferRangeExpected(use.bufferRange, &entry.bufferDesc);
                                           return written && written->offset < buffer->offset + buffer->size &&
                                                  buffer->offset < written->offset + written->size;
                                       });
        };
        size_t writer = SIZE_MAX;
        for (size_t index = 0; index < m_Passes.size(); ++index)
        {
            const auto& pass = m_Passes[index];
            if (passName.empty())
            {
                if (writesSource(pass))
                {
                    writer = index;
                }
            }
            else if (pass.name == passName)
            {
                if (writer != SIZE_MAX)
                {
                    return std::unexpected("Capture requires one pass named: " + passName);
                }
                writer = index;
            }
        }
        if (writer == SIZE_MAX)
        {
            return std::unexpected(passName.empty() ? "Captured range has no writer: " + entry.name :
                                                      "Capture requires one pass named: " + passName);
        }
        if (m_ActiveAppend && writer < m_ActiveAppend->m_Passes)
        {
            return std::unexpected("Cannot insert a capture after a pass that predates this graph construction");
        }
        if (!writesSource(m_Passes[writer]))
        {
            return std::unexpected(passName + " does not write the captured range");
        }
        const VriBufferDesc bufferDesc {buffer->size,
                                        0,
                                        VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst |
                                            (entry.bufferDesc.usage & VriBufferUsage_ConcurrentQueues),
                                        VriMemoryLocation_Device};
        const auto        label = "Capture after " + m_Passes[writer].name;
        AppendTransaction transaction(*this);
        auto                created = isTexture ? createTextureExpected(std::move(name), textureDesc) :
                                                  createBufferExpected(std::move(name), bufferDesc);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        const auto captured = *created;
        const Use        sourceUse {source, Usage::eCopySource, false, isTexture ? *texture : TextureRange {}, *buffer};
        const std::array uses {sourceUse, Use {captured, Usage::eCopyDestination, true, destination}};
        auto             added = addPassExpected(
            label,
            uses,
            [this, source, captured, isTexture, texture = *texture, buffer = *buffer](auto* cmd, RenderGraph& graph)
                -> std::expected<void, std::string>
            {
                if (!isTexture)
                {
                    auto input  = graph.getBufferExpected(source);
                    auto output = graph.getBufferExpected(captured);
                    if (!input || !output)
                    {
                        return std::unexpected(input ? output.error() : input.error());
                    }
                    const VriBufferCopyDesc copy {buffer.offset, 0, buffer.size};
                    m_Device.core.CmdCopyBuffer(cmd, (*output)->handle, (*input)->handle, &copy);
                    return {};
                }
                auto input  = graph.getTextureExpected(source);
                auto output = graph.getTextureExpected(captured);
                if (!input || !output)
                {
                    return std::unexpected(input ? output.error() : input.error());
                }
                const auto& desc    = (*input)->desc;
                for (uint32_t layer = 0; layer < texture.layerNum; ++layer)
                {
                    for (uint32_t mip = 0; mip < texture.mipNum; ++mip)
                    {
                        for (auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                        {
                            if (!(texture.aspect & aspect))
                            {
                                continue;
                            }
                            VriTextureCopyDesc copy {};
                            copy.src.mip       = texture.baseMip + mip;
                            copy.src.baseLayer = texture.baseLayer + layer;
                            copy.src.layerNum  = 1;
                            copy.src.aspect    = aspect;
                            copy.src.width     = std::max(desc.width >> copy.src.mip, 1u);
                            copy.src.height    = std::max(desc.height >> copy.src.mip, 1u);
                            copy.src.depth     = std::max(desc.depth >> copy.src.mip, 1u);
                            copy.dst           = copy.src;
                            copy.dst.mip       = mip;
                            copy.dst.baseLayer = layer;
                            m_Device.core.CmdCopyTexture(cmd, (*output)->handle, (*input)->handle, &copy);
                        }
                    }
                }
                return {};
            });
        if (!added)
        {
            return std::unexpected(added.error());
        }
        if (auto exported = exportRangeExpected({captured, destination}); !exported)
        {
            return std::unexpected(exported.error());
        }
        m_Passes.back().capturesTexture = isTexture;
        const auto insertion = writer + 1;
        // Validate/append first: an Expected failure must not reorder the preceding graph.
        std::rotate(m_Passes.begin() + insertion, m_Passes.end() - 1, m_Passes.end());
        for (auto& pass : m_Passes)
        {
            for (auto& dependency : pass.orderingDependencies)
            {
                if (dependency >= insertion)
                {
                    ++dependency;
                }
            }
        }
        transaction.commit();
        return captured;
    }

    RenderGraph::Resource
    RenderGraph::createHistoryTexture(std::string name, const VriTextureDesc& desc, VriClearValue initial)
    {
        auto created = createHistoryTextureExpected(std::move(name), desc, initial);
        if (!created)
        {
            throw std::invalid_argument(created.error()); // Legacy boundary.
        }
        return *created;
    }

    std::expected<RenderGraph::Resource, std::string>
    RenderGraph::createHistoryTextureExpected(std::string name, const VriTextureDesc& desc, VriClearValue initial)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (desc.memoryLocation == VriMemoryLocation_Undefined)
        {
            return std::unexpected("History textures require an allocated memory location");
        }
        const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
        const auto  aspects = textureAspects(desc.format);
        if (aspects == VriImageAspect_Color)
        {
            if (!(desc.usage & VriTextureUsage_TransferDst) || !(desc.usage & VriTextureUsage_ShaderResourceStorage) ||
                desc.sampleNum != 1 || !caps || !caps->hasClearStorageTextureRange ||
                !m_Device.core.CmdClearStorageTextureRange)
            {
                return std::unexpected("Color history requires single-sample storage/transfer-destination usage and "
                                       "native range clear support");
            }
        }
        else
        {
            const auto required = VriTextureUsage_DepthStencilAttachment | VriTextureUsage_TransferDst;
            if ((desc.usage & required) != required || !caps || !caps->hasClearDepthStencilTextureRange ||
                !m_Device.core.CmdClearDepthStencilTextureRange ||
                ((aspects & VriImageAspect_Depth) &&
                 (!std::isfinite(initial.depthStencil.depth) || initial.depthStencil.depth < 0 ||
                  initial.depthStencil.depth > 1)) ||
                ((aspects & VriImageAspect_Stencil) && initial.depthStencil.stencil > 255))
            {
                return std::unexpected("Depth/stencil history requires valid clear values, "
                                       "attachment/transfer-destination usage and native range clear support");
            }
        }
        auto resource = createTextureExpected(std::move(name), desc);
        if (!resource)
        {
            return std::unexpected(resource.error());
        }
        auto& entry               = m_Resources[resource->index];
        entry.history             = true;
        entry.initialized         = true;
        entry.initial             = initial;
        entry.historyEpoch        = m_HistoryEpoch;
        entry.historyResetPending = true;
        entry.historyCells.assign(size_t(desc.mipNum) * desc.layerNum * std::popcount(aspects), {m_HistoryEpoch, true});
        return *resource;
    }

    std::expected<RenderGraph::Resource, std::string>
    RenderGraph::createHistoryBufferExpected(std::string                name,
                                             const VriBufferDesc&       desc,
                                             std::span<const std::byte> initial)
    {
        if (auto editable = checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
        if (!caps || !desc.size || desc.size > caps->bufferMaxSize || desc.size > SIZE_MAX ||
            desc.memoryLocation < VriMemoryLocation_Device || desc.memoryLocation >= VriMemoryLocation_Undefined ||
            !(desc.usage & VriBufferUsage_TransferDst) || (!initial.empty() && initial.size() != desc.size))
        {
            return std::unexpected("Buffer history requires allocated TransferDst storage within device/CPU limits "
                                   "and an empty or exact-size initial byte snapshot");
        }
        // A zero initialization can reuse the same bytes for every destination chunk.
        constexpr uint64_t zeroChunkBytes = 256 * 1024;
        const uint64_t     initialSize    = initial.empty() ? std::min(desc.size, zeroChunkBytes) : desc.size;
        auto               source         = Buffer::create(m_Device,
                                                           {initialSize,
                                                            0,
                                                            VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues,
                                                            VriMemoryLocation_HostUpload});
        if (!source)
        {
            return std::unexpected("Create buffer history initial snapshot: " + source.error());
        }
        auto* mapped = m_Device.core.MapBuffer((*source)->handle, 0, initialSize);
        if (!mapped)
        {
            return std::unexpected("Map buffer history initial snapshot failed");
        }
        if (initial.empty())
        {
            std::memset(mapped, 0, size_t(initialSize));
        }
        else
        {
            std::memcpy(mapped, initial.data(), initial.size());
        }
        m_Device.core.UnmapBuffer((*source)->handle);
        // Immutable host writes are visible at submission. This source has no GPU writer or mutable public view.
        (*source)->setState({VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        auto created = createBufferExpected(std::move(name), desc);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        auto& entry                = m_Resources[created->index];
        entry.history              = true;
        entry.initialized          = true;
        entry.historyEpoch         = m_HistoryEpoch;
        entry.historyResetPending  = true;
        entry.historyInitialBuffer = std::move(*source);
        entry.bufferHistory.push_back({{0, desc.size}, {m_HistoryEpoch, true}});
        return created;
    }

    std::expected<void, std::string> RenderGraph::resetHistoryExpected()
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        std::vector<HistorySelection> selections;
        for (size_t r = 0; r < m_Resources.size(); ++r)
        {
            if (m_Resources[r].history)
            {
                selections.push_back({{this, uint32_t(r)}, {}});
            }
        }
        if (!selections.empty())
        {
            return resetHistoryExpected(selections);
        }
        if (m_HistoryEpoch == UINT64_MAX)
        {
            return std::unexpected("Graph history epoch exhausted");
        }
        ++m_HistoryEpoch;
        return {};
    }

    std::expected<void, std::string> RenderGraph::resetHistoryExpected(std::span<const Resource> resources)
    {
        std::vector<HistorySelection> selections;
        selections.reserve(resources.size());
        for (const auto resource : resources)
        {
            selections.push_back({resource, {}});
        }
        return resetHistoryExpected(selections);
    }

    std::expected<void, std::string> RenderGraph::resetHistoryExpected(std::span<const HistorySelection> selections)
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        if (selections.empty())
        {
            return {};
        }
        std::vector<HistorySelection> normalized;
        normalized.reserve(selections.size());
        for (const auto& selected : selections)
        {
            if (!ownsResource(selected.resource) || !m_Resources[selected.resource.index].history)
            {
                return std::unexpected("History reset requires history resources owned by this graph");
            }
            const auto& entry = m_Resources[selected.resource.index];
            auto        range = normalizedRangeExpected(selected.range, entry.isTexture ? &entry.textureDesc : nullptr);
            if (!range)
            {
                return std::unexpected(range.error());
            }
            auto bytes =
                normalizedBufferRangeExpected(selected.bufferRange, entry.isTexture ? nullptr : &entry.bufferDesc);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
            if (!entry.isTexture && (bytes->offset || bytes->size != entry.bufferDesc.size) &&
                (!caps || !caps->hasBufferRangeBarriers || !m_Device.core.CmdBufferBarrierRange))
            {
                return std::unexpected("Selected buffer history reset requires native byte-range barriers");
            }
            const auto& desc = entry.textureDesc;
            if (entry.isTexture && std::popcount(textureAspects(desc.format)) > 1 &&
                range->aspect != textureAspects(desc.format))
            {
                if (!caps || !caps->hasSeparateDepthStencilLayouts)
                {
                    return std::unexpected("Selected depth/stencil history reset requires independent aspect layouts");
                }
            }
            normalized.push_back({selected.resource, *range, *bytes});
        }
        if (m_HistoryEpoch == UINT64_MAX)
        {
            return std::unexpected("Graph history epoch exhausted");
        }
        ++m_HistoryEpoch;
        for (const auto& selected : normalized)
        {
            auto& entry               = m_Resources[selected.resource.index];
            entry.historyEpoch        = m_HistoryEpoch;
            entry.historyResetPending = true;
            if (!entry.isTexture)
            {
                resetBufferHistory(entry.bufferHistory, selected.bufferRange, m_HistoryEpoch);
                continue;
            }
            const auto& range         = selected.range;
            visitTextureCells(entry.textureDesc,
                              range,
                              [&](size_t cell)
                              {
                                  entry.historyCells[cell] = {m_HistoryEpoch, true};
                              });
        }
        return {};
    }

    std::expected<RenderGraph::HistoryInfo, std::string> RenderGraph::historyInfoExpected(Resource     resource,
                                                                                          TextureRange selected) const
    {
        if (!ownsResource(resource) || !m_Resources[resource.index].history || !m_Resources[resource.index].isTexture)
        {
            return std::unexpected("History query requires a history resource owned by this graph");
        }
        const auto& entry = m_Resources[resource.index];
        auto        range = normalizedRangeExpected(selected, &entry.textureDesc);
        if (!range)
        {
            return std::unexpected(range.error());
        }
        HistoryInfo result;
        visitTextureCells(entry.textureDesc,
                          *range,
                          [&](size_t index)
                          {
                              const auto& cell = entry.historyCells[index];
                              result.epoch     = std::max(result.epoch, cell.epoch);
                              result.resetPending |= cell.resetPending;
                          });
        return result;
    }

    std::expected<RenderGraph::HistoryInfo, std::string>
    RenderGraph::bufferHistoryInfoExpected(Resource resource, BufferRange selected) const
    {
        if (!ownsResource(resource) || !m_Resources[resource.index].history || m_Resources[resource.index].isTexture)
        {
            return std::unexpected("Buffer history query requires a buffer history owned by this graph");
        }
        const auto& entry = m_Resources[resource.index];
        auto        range = normalizedBufferRangeExpected(selected, &entry.bufferDesc);
        if (!range)
        {
            return std::unexpected(range.error());
        }
        HistoryInfo result;
        for (const auto& current : entry.bufferHistory)
        {
            if (current.range.offset < range->offset + range->size &&
                range->offset < current.range.offset + current.range.size)
            {
                result.epoch = std::max(result.epoch, current.state.epoch);
                result.resetPending |= current.state.resetPending;
            }
        }
        return result;
    }

    std::expected<void, std::string>
    RenderGraph::resetDependentHistoryExpected(std::span<const std::string_view> changedPasses,
                                               std::span<const ResourceRange>    changedImports)
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        if (changedPasses.empty() && changedImports.empty())
        {
            return {};
        }
        if (!m_Compiled)
        {
            return std::unexpected("Compile the graph before invalidating dependent history");
        }
        std::vector<bool> affected(m_Passes.size());
        for (const auto name : changedPasses)
        {
            const auto found = std::ranges::find(m_Passes, name, &Pass::name);
            if (found == m_Passes.end())
            {
                return std::unexpected("Unknown changed graph pass: " + std::string(name));
            }
            if (std::ranges::count(m_Passes, name, &Pass::name) != 1)
            {
                return std::unexpected("Ambiguous changed graph pass: " + std::string(name));
            }
            affected[size_t(found - m_Passes.begin())] = found->live;
        }
        if (!changedImports.empty())
        {
            const auto visit = [&](const ResourceRange& selection, const auto& cell) -> std::expected<void, std::string>
            {
                const auto& entry = m_Resources[selection.resource.index];
                auto range = normalizedRangeExpected(selection.range, entry.isTexture ? &entry.textureDesc : nullptr);
                auto bytes =
                    normalizedBufferRangeExpected(selection.bufferRange, entry.isTexture ? nullptr : &entry.bufferDesc);
                if (!range || !bytes)
                {
                    return std::unexpected(entry.name + ": " + (!range ? range.error() : bytes.error()));
                }
                if (entry.isTexture)
                {
                    visitTextureCells(entry.textureDesc, *range, cell);
                }
                else
                {
                    visitBufferCells(entry.bufferBoundaries, *bytes, cell);
                }
                return {};
            };
            std::vector<std::vector<bool>> incoming(m_Resources.size());
            for (const auto& selection : changedImports)
            {
                if (!ownsResource(selection.resource) || !m_Resources[selection.resource.index].imported)
                {
                    return std::unexpected("Incoming content edits require imports owned by this graph");
                }
                const auto& entry = m_Resources[selection.resource.index];
                auto&       dirty = incoming[selection.resource.index];
                if (dirty.empty())
                {
                    const auto& desc = entry.textureDesc;
                    dirty.resize(entry.isTexture ?
                                     size_t(desc.mipNum) * desc.layerNum * std::popcount(textureAspects(desc.format)) :
                                     entry.bufferBoundaries.size() - 1);
                }
                if (auto marked = visit(selection,
                                        [&](size_t cell)
                                        {
                                            dirty[cell] = entry.initialized;
                                        });
                    !marked)
                {
                    return marked;
                }
            }
            // Once a pass writes a cell, its consumers use the existing compiled content edges.
            // Seed only the live passes that see changed data before its first write.
            for (size_t p = 0; p < m_Passes.size(); ++p)
            {
                const auto& pass = m_Passes[p];
                if (!pass.live)
                {
                    continue;
                }
                for (const auto& use : pass.uses)
                {
                    auto& dirty = incoming[use.resource.index];
                    if (dirty.empty())
                    {
                        continue;
                    }
                    const auto consume = [&](size_t cell)
                    {
                        if (dirty[cell])
                        {
                            affected[p] = affected[p] || !use.discardPrevious;
                            dirty[cell] = !writes(use.usage);
                        }
                    };
                    if (auto visited = visit({use.resource, use.range, use.bufferRange}, consume); !visited)
                    {
                        return visited;
                    }
                }
            }
        }
        if (std::ranges::none_of(affected,
                                 [](bool value)
                                 {
                                     return value;
                                 }))
        {
            return {};
        }
        std::vector<std::vector<bool>> histories(m_Resources.size());
        for (size_t r = 0; r < m_Resources.size(); ++r)
        {
            const auto& entry = m_Resources[r];
            if (entry.history)
            {
                histories[r].resize(entry.isTexture ? entry.historyCells.size() : entry.bufferBoundaries.size() - 1);
            }
        }
        // A later history writer can invalidate an earlier reader in the next frame. Iterate that feedback
        // to a fixed point; ordinary intra-frame content edges remain in declaration order.
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (size_t p = 0; p < m_Passes.size(); ++p)
            {
                const auto& pass = m_Passes[p];
                if (!pass.live)
                {
                    continue;
                }
                if (!affected[p])
                {
                    affected[p] = std::ranges::any_of(pass.contentDependencies,
                                                      [&](size_t dependency)
                                                      {
                                                          return affected[dependency];
                                                      }) ||
                                  std::ranges::any_of(pass.historyDependencies,
                                                      [&](const auto& dependency)
                                                      {
                                                          return histories[dependency.first][dependency.second];
                                                      });
                    changed |= affected[p];
                }
                if (!affected[p])
                {
                    continue;
                }
                for (const auto& [resource, cell] : pass.finalHistoryWrites)
                {
                    changed |= !histories[resource][cell];
                    histories[resource][cell] = true;
                }
            }
        }
        std::vector<HistorySelection> selection;
        for (size_t r = 0; r < histories.size(); ++r)
        {
            for (size_t cell = 0; cell < histories[r].size(); ++cell)
            {
                if (histories[r][cell])
                {
                    const auto& entry = m_Resources[r];
                    if (entry.isTexture)
                    {
                        selection.push_back({{this, uint32_t(r)}, textureCellRange(entry.textureDesc, cell)});
                    }
                    else
                    {
                        const auto begin = entry.bufferBoundaries[cell];
                        selection.push_back(
                            {{this, uint32_t(r)}, {}, {begin, entry.bufferBoundaries[cell + 1] - begin}});
                    }
                }
            }
        }
        return resetHistoryExpected(selection);
    }

    void RenderGraph::resetHistory()
    {
        if (auto reset = resetHistoryExpected(); !reset)
        {
            throw std::overflow_error(reset.error()); // Legacy boundary.
        }
    }

    uint64_t RenderGraph::historyEpoch() const
    {
        return m_HistoryEpoch;
    }

    std::expected<void, std::string> RenderGraph::addOrderingExpected(std::string_view before, std::string_view after)
    {
        if (auto idle = checkStateTransactionIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        if (m_Aliased)
        {
            return std::unexpected("An aliased graph must keep its dependency plan");
        }
        const auto find = [&](std::string_view name) -> std::expected<size_t, std::string>
        {
            size_t found = SIZE_MAX;
            for (size_t p = 0; p < m_Passes.size(); ++p)
            {
                if (m_Passes[p].name == name)
                {
                    if (found != SIZE_MAX)
                    {
                        return std::unexpected("Ambiguous pass name: " + std::string(name));
                    }
                    found = p;
                }
            }
            if (found == SIZE_MAX)
            {
                return std::unexpected("Unknown pass name: " + std::string(name));
            }
            return found;
        };
        auto source      = find(before);
        auto destination = find(after);
        if (!source || !destination)
        {
            return std::unexpected(!source ? source.error() : destination.error());
        }
        if (*source >= *destination)
        {
            return std::unexpected("Ordering edges must follow pass declaration order");
        }
        if (m_ActiveAppend && *destination < m_ActiveAppend->m_Passes)
        {
            return std::unexpected("Cannot add ordering to a pass that predates this graph construction");
        }
        auto& dependencies = m_Passes[*destination].orderingDependencies;
        if (std::ranges::find(dependencies, *source) == dependencies.end())
        {
            dependencies.push_back(*source);
            m_Compiled = false;
        }
        return {};
    }

    std::expected<void, std::string> RenderGraph::planPassesExpected()
    {
        struct Cell
        {
            std::optional<size_t> writer;
            std::vector<size_t>   readers;
            size_t                usedInPass = SIZE_MAX;
            std::optional<size_t> captureReader;
        };

        // Partition buffers only at declared endpoints, never once per byte. Include holes for export checks.
        for (auto& entry : m_Resources)
        {
            entry.bufferBoundaries.clear();
            if (!entry.isTexture)
            {
                if (!entry.bufferDesc.size)
                {
                    return std::unexpected("Buffer has zero byte size: " + entry.name);
                }
                entry.bufferBoundaries = {0, entry.bufferDesc.size};
            }
        }
        const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
        for (const auto& pass : m_Passes)
        {
            for (const auto& use : pass.uses)
            {
                if (!ownsResource(use.resource))
                {
                    return std::unexpected(pass.name + ": resource belongs to another graph");
                }
                auto& entry = m_Resources[use.resource.index];
                auto  range =
                    normalizedBufferRangeExpected(use.bufferRange, entry.isTexture ? nullptr : &entry.bufferDesc);
                if (!range)
                {
                    return std::unexpected(pass.name + ": " + range.error() + ": " + entry.name);
                }
                if (!entry.isTexture)
                {
                    if ((range->offset || range->size != entry.bufferDesc.size) &&
                        (!caps || !caps->hasBufferRangeBarriers || !m_Device.core.CmdBufferBarrierRange))
                    {
                        return std::unexpected(pass.name + ": native buffer byte-range barriers are not enabled");
                    }
                    entry.bufferBoundaries.push_back(range->offset);
                    entry.bufferBoundaries.push_back(range->offset + range->size);
                }
            }
        }
        for (const auto& selection : m_Exports)
        {
            auto& entry = m_Resources[selection.resourceIndex];
            if (!entry.isTexture)
            {
                entry.bufferBoundaries.push_back(selection.bufferRange.offset);
                entry.bufferBoundaries.push_back(selection.bufferRange.offset + selection.bufferRange.size);
            }
        }
        std::vector<std::vector<Cell>> cells(m_Resources.size());
        for (size_t r = 0; r < m_Resources.size(); ++r)
        {
            auto& entry      = m_Resources[r];
            auto& boundaries = entry.bufferBoundaries;
            std::ranges::sort(boundaries);
            boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
            const auto count = entry.isTexture ? size_t(entry.textureDesc.mipNum) * entry.textureDesc.layerNum *
                                                     std::popcount(textureAspects(entry.textureDesc.format)) :
                                                 boundaries.size() - 1;
            cells[r].resize(count);
        }
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            auto& pass = m_Passes[p];
            pass.dependencies = pass.orderingDependencies;
            pass.contentDependencies.clear();
            pass.historyDependencies.clear();
            pass.finalHistoryWrites.clear();
            pass.live = false;
            for (const auto& use : pass.uses)
            {
                const auto  r     = use.resource.index;
                const auto& entry = m_Resources[r];
                if (use.discardPrevious && (!writes(use.usage) || reads(use.usage)))
                {
                    return std::unexpected(pass.name + ": discardPrevious requires a write-only usage: " + entry.name);
                }
                auto range = normalizedRangeExpected(use.range, entry.isTexture ? &entry.textureDesc : nullptr);
                if (!range)
                {
                    return std::unexpected(pass.name + ": " + range.error() + ": " + entry.name);
                }
                auto required = requiredUsageExpected(use.usage, entry.isTexture);
                if (!required)
                {
                    return std::unexpected(pass.name + ": " + required.error());
                }
                const auto declared = entry.isTexture ? entry.textureDesc.usage : entry.bufferDesc.usage;
                if ((declared & *required) != *required)
                {
                    return std::unexpected(pass.name + ": resource lacks required usage flags: " + entry.name);
                }
                const auto visit = [&](size_t index) -> std::expected<void, std::string>
                {
                    auto& cell = cells[r][index];
                    if (cell.usedInPass == p)
                    {
                        return std::unexpected(pass.name +
                                               ": overlapping resource uses; use a ReadWrite usage: " + entry.name);
                    }
                    cell.usedInPass = p;
                    if (reads(use.usage) && !cell.writer && !entry.initialized)
                    {
                        std::string location;
                        if (entry.isTexture)
                        {
                            const auto planes = size_t(std::popcount(textureAspects(entry.textureDesc.format)));
                            location          = " mip " + std::to_string((index / planes) % entry.textureDesc.mipNum) +
                                       " layer " + std::to_string(index / (planes * entry.textureDesc.mipNum));
                        }
                        else
                        {
                            location = " bytes " + std::to_string(entry.bufferBoundaries[index]) + ".." +
                                       std::to_string(entry.bufferBoundaries[index + 1]);
                        }
                        return std::unexpected(pass.name + ": read before first write: " + entry.name + location);
                    }
                    if (entry.history && !cell.writer && (reads(use.usage) || !use.discardPrevious))
                    {
                        pass.historyDependencies.emplace_back(uint32_t(r), index);
                    }
                    if (cell.writer)
                    {
                        pass.dependencies.push_back(*cell.writer);
                        if (reads(use.usage) || !use.discardPrevious)
                        {
                            pass.contentDependencies.push_back(*cell.writer);
                        }
                    }
                    if (cell.captureReader)
                    {
                        pass.dependencies.push_back(*cell.captureReader);
                    }
                    if (writes(use.usage))
                    {
                        pass.dependencies.insert(pass.dependencies.end(), cell.readers.begin(), cell.readers.end());
                        cell.readers.clear();
                        cell.writer = p;
                        cell.captureReader.reset();
                    }
                    else
                    {
                        if (pass.capturesTexture && use.usage == Usage::eCopySource)
                        {
                            // CopySource can replace the layout shared by earlier readers. Keep ordering
                            // separate from content so a capture does not make otherwise unused readers live.
                            pass.dependencies.insert(pass.dependencies.end(), cell.readers.begin(), cell.readers.end());
                            cell.captureReader = p;
                        }
                        cell.readers.push_back(p);
                    }
                    return {};
                };
                if (!entry.isTexture)
                {
                    auto bytes = normalizedBufferRangeExpected(use.bufferRange, &entry.bufferDesc);
                    if (!bytes)
                    {
                        return std::unexpected(bytes.error());
                    }
                    const auto begin = std::ranges::lower_bound(entry.bufferBoundaries, bytes->offset);
                    const auto end   = std::ranges::lower_bound(entry.bufferBoundaries, bytes->offset + bytes->size);
                    for (auto cell = begin; cell != end; ++cell)
                    {
                        if (auto visited = visit(size_t(cell - entry.bufferBoundaries.begin())); !visited)
                        {
                            return visited;
                        }
                    }
                    continue;
                }
                const auto available          = textureAspects(entry.textureDesc.format);
                const auto selected           = range->aspect;
                const bool resolveDestination = use.usage == Usage::eDepthResolveDestination;
                if (resolveDestination || use.usage == Usage::eDepthWriteResolve ||
                    use.usage == Usage::eDepthReadWriteResolve)
                {
                    const auto& desc = entry.textureDesc;
                    if (available == VriImageAspect_Color || selected != available ||
                        (desc.type != VriTextureType_2D && desc.type != VriTextureType_2DArray) ||
                        (resolveDestination ? desc.sampleNum != 1 : desc.sampleNum <= 1))
                    {
                        return std::unexpected(
                            pass.name +
                            ": invalid depth/stencil resolve aspect, dimension or sample count: " + entry.name);
                    }
                    if (!caps || ((selected & VriImageAspect_Depth) && !caps->hasDepthAttachmentResolve) ||
                        ((selected & VriImageAspect_Stencil) && !caps->hasStencilAttachmentResolve))
                    {
                        return std::unexpected(pass.name + ": native depth/stencil attachment resolve is unavailable");
                    }
                }
                const auto planeCount = size_t(std::popcount(available));
                if (planeCount > 1 && selected != available && (!caps || !caps->hasSeparateDepthStencilLayouts))
                {
                    return std::unexpected(pass.name + ": device does not enable independent depth/stencil layouts");
                }
                size_t plane = 0;
                for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    if (!(available & aspect))
                    {
                        continue;
                    }
                    if (selected & aspect)
                    {
                        for (uint32_t layer = range->baseLayer; layer < range->baseLayer + range->layerNum; ++layer)
                        {
                            for (uint32_t mip = range->baseMip; mip < range->baseMip + range->mipNum; ++mip)
                            {
                                const auto index =
                                    (size_t(layer) * entry.textureDesc.mipNum + mip) * planeCount + plane;
                                if (auto visited = visit(index); !visited)
                                {
                                    return visited;
                                }
                            }
                        }
                    }
                    ++plane;
                }
            }
            const auto unique = [](auto& values)
            {
                std::ranges::sort(values);
                values.erase(std::unique(values.begin(), values.end()), values.end());
            };
            unique(pass.dependencies);
            unique(pass.contentDependencies);
            unique(pass.historyDependencies);
        }
        std::vector<size_t> pending;
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            if (m_Passes[p].sideEffect)
            {
                pending.push_back(p);
            }
        }
        for (size_t r = 0; r < m_Resources.size(); ++r)
        {
            if (m_Resources[r].history)
            {
                for (size_t cell = 0; cell < cells[r].size(); ++cell)
                {
                    if (const auto writer = cells[r][cell].writer)
                    {
                        // The last write is the version carried to the next frame.
                        m_Passes[*writer].finalHistoryWrites.emplace_back(uint32_t(r), cell);
                        pending.push_back(*writer);
                    }
                }
            }
        }
        for (const auto& selection : m_Exports)
        {
            const auto& entry         = m_Resources[selection.resourceIndex];
            bool        uninitialized = false;
            const auto  visit         = [&](size_t index)
            {
                const auto& cell = cells[selection.resourceIndex][index];
                if (cell.writer)
                {
                    pending.push_back(*cell.writer);
                }
                else if (!entry.initialized)
                {
                    uninitialized = true;
                }
            };
            if (entry.isTexture)
            {
                visitTextureCells(entry.textureDesc, selection.range, visit);
            }
            else
            {
                const auto begin = std::ranges::lower_bound(entry.bufferBoundaries, selection.bufferRange.offset);
                const auto end   = std::ranges::lower_bound(entry.bufferBoundaries,
                                                          selection.bufferRange.offset + selection.bufferRange.size);
                for (auto cell = begin; cell != end; ++cell)
                {
                    visit(size_t(cell - entry.bufferBoundaries.begin()));
                }
            }
            if (uninitialized)
            {
                return std::unexpected(std::string("Exported resource has uninitialized ") +
                                       (entry.isTexture ? "mip/layer cells: " : "buffer bytes: ") + entry.name);
            }
        }
        while (!pending.empty())
        {
            auto& pass = m_Passes[pending.back()];
            pending.pop_back();
            if (!pass.live)
            {
                pass.live = true;
                pending.insert(pending.end(), pass.contentDependencies.begin(), pass.contentDependencies.end());
                pending.insert(pending.end(), pass.orderingDependencies.begin(), pass.orderingDependencies.end());
            }
        }
        const auto ancestorWords = (m_Passes.size() + 63) / 64;
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            auto& pass = m_Passes[p];
            pass.ancestors.assign(ancestorWords, 0);
            if (!pass.live)
            {
                continue;
            }
            for (const auto dependency : pass.dependencies)
            {
                if (!m_Passes[dependency].live)
                {
                    continue;
                }
                pass.ancestors[dependency / 64] |= uint64_t(1) << (dependency % 64);
                for (size_t word = 0; word < ancestorWords; ++word)
                {
                    pass.ancestors[word] |= m_Passes[dependency].ancestors[word];
                }
            }
        }
        return {};
    }

    void RenderGraph::compile(bool aliasTransients)
    {
        if (auto compiled = compileExpected(aliasTransients); !compiled)
        {
            throw std::runtime_error(compiled.error()); // Legacy boundary.
        }
    }

    std::expected<void, std::string> RenderGraph::compileExpected(bool aliasTransients)
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        if (m_RecoveryFailed)
        {
            return std::unexpected("Rebuild the graph after failed pass-state recovery");
        }

        if (m_Aliased)
        {
            if (!aliasTransients)
            {
                return std::unexpected("An aliased graph must keep its allocation plan");
            }
            return {};
        }
        if (aliasTransients && std::ranges::any_of(m_Resources,
                                                   [](const Entry& entry)
                                                   {
                                                       return entry.ownedTexture || entry.ownedBuffer;
                                                   }))
        {
            return std::unexpected("Choose transient aliasing before the first graph compilation");
        }
        const bool newPlan = !m_Compiled || aliasTransients;
        if (newPlan && m_PlanEpoch == UINT64_MAX)
        {
            return std::unexpected("Graph compilation epoch exhausted");
        }
        m_Compiled = false;
        if (auto planned = planPassesExpected(); !planned)
        {
            return planned;
        }
        // Declaration order is topological. Imports, exports and history never share an allocation.
        for (auto& entry : m_Resources)
        {
            entry.firstUse = UINT32_MAX;
            entry.lastUse  = UINT32_MAX;
        }
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            if (m_Passes[p].live)
            {
                for (const auto& use : m_Passes[p].uses)
                {
                    auto& entry    = m_Resources[use.resource.index];
                    entry.firstUse = std::min(entry.firstUse, uint32_t(p));
                    entry.lastUse  = uint32_t(p);
                }
            }
        }
        std::vector<uint32_t> order;
        for (uint32_t i = 0; i < m_Resources.size(); ++i)
        {
            // An exported, read-only history/import is live even without a pass.
            if (m_Resources[i].firstUse != UINT32_MAX || isExported(i))
            {
                order.push_back(i);
            }
        }
        std::ranges::stable_sort(order,
                                 [this](uint32_t a, uint32_t b)
                                 {
                                     return m_Resources[a].firstUse < m_Resources[b].firstUse;
                                 });
        std::vector<uint32_t> allocations;
        std::vector<uint32_t> ends(m_Resources.size(), UINT32_MAX);
        std::vector<uint32_t> created;
        const auto            rollback = [&]
        {
            for (auto& resource : m_Resources)
            {
                if (!resource.imported && resource.allocation != UINT32_MAX &&
                    resource.allocation != uint32_t(&resource - m_Resources.data()))
                {
                    resource.texture = nullptr, resource.buffer = nullptr;
                }
            }
            for (const auto allocated : created)
            {
                auto& resource   = m_Resources[allocated];
                resource.texture = nullptr;
                resource.buffer  = nullptr;
                resource.ownedTexture.reset();
                resource.ownedBuffer.reset();
            }
        };
        for (const auto index : order)
        {
            auto& entry      = m_Resources[index];
            entry.allocation = index;
            if (entry.imported)
            {
                continue;
            }
            const auto location = entry.isTexture ? entry.textureDesc.memoryLocation : entry.bufferDesc.memoryLocation;
            if (aliasTransients && location == VriMemoryLocation_Device && !entry.history && !entry.preserve &&
                !isExported(index))
            {
                for (const auto owner : allocations)
                {
                    const auto& candidate = m_Resources[owner];
                    if (ends[owner] >= entry.firstUse || candidate.history || candidate.preserve || isExported(owner) ||
                        candidate.isTexture != entry.isTexture)
                    {
                        continue;
                    }
                    const auto& a = candidate.textureDesc;
                    const auto& b = entry.textureDesc;
                    const auto& c = candidate.bufferDesc;
                    const auto& d = entry.bufferDesc;
                    // Compare fields, not struct padding. Clear hints also belong to the physical texture.
                    const bool same = entry.isTexture ?
                                          a.type == b.type && a.format == b.format && a.width == b.width &&
                                              a.height == b.height && a.depth == b.depth && a.mipNum == b.mipNum &&
                                              a.layerNum == b.layerNum && a.sampleNum == b.sampleNum &&
                                              a.usage == b.usage && a.memoryLocation == b.memoryLocation &&
                                              std::memcmp(&a.clearValue, &b.clearValue, sizeof(VriClearValue)) == 0 :
                                          c.size == d.size && c.structureStride == d.structureStride &&
                                              c.usage == d.usage && c.memoryLocation == d.memoryLocation;
                    if (same)
                    {
                        entry.texture    = candidate.texture;
                        entry.buffer     = candidate.buffer;
                        entry.allocation = owner;
                        ends[owner]      = entry.lastUse;
                        break;
                    }
                }
            }
            if (!entry.texture && !entry.buffer)
            {
                if (entry.isTexture)
                {
                    auto texture = Texture::create(m_Device, entry.textureDesc);
                    if (!texture)
                    {
                        rollback();
                        return std::unexpected(texture.error());
                    }
                    entry.ownedTexture = std::move(*texture);
                    entry.texture      = entry.ownedTexture.get();
                    m_Device.core.SetDebugName(entry.texture->handle, entry.name.c_str());
                }
                else
                {
                    auto buffer = Buffer::create(m_Device, entry.bufferDesc);
                    if (!buffer)
                    {
                        rollback();
                        return std::unexpected(buffer.error());
                    }
                    entry.ownedBuffer = std::move(*buffer);
                    entry.buffer      = entry.ownedBuffer.get();
                    m_Device.core.SetDebugName(entry.buffer->handle, entry.name.c_str());
                }
                created.push_back(index);
            }
            if (entry.allocation == index)
            {
                allocations.push_back(index);
                ends[index] = entry.lastUse;
            }
        }
        m_Aliased  = aliasTransients;
        m_Compiled = true;
        m_PlanEpoch += uint64_t(newPlan);
        return {};
    }

    std::expected<void, std::string> RenderGraph::compileHeapsExpected()
    {
        return compileHeapsExpected(nullptr, {}, {});
    }

    std::expected<void, std::string>
    RenderGraph::compileHeapsExpected(VriCommandBuffer*                    initial,
                                      std::span<const PassRecording>       recordings,
                                      std::span<const RecordingDependency> submissionOrder)
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        if (m_RecoveryFailed)
        {
            return std::unexpected("Rebuild the graph after failed pass-state recovery");
        }

        if (!initial && (!recordings.empty() || !submissionOrder.empty()))
        {
            return std::unexpected("Proposed heap submission order requires an initial command buffer");
        }
        if (m_HeapAliasing)
        {
            if (initial)
            {
                return std::unexpected("Choose a proposed heap submission order before the first heap compilation");
            }
            return {};
        }
        if (m_PlanEpoch == UINT64_MAX)
        {
            return std::unexpected("Graph compilation epoch exhausted");
        }
        if (m_Aliased || std::ranges::any_of(m_Resources,
                                             [](const Entry& entry)
                                             {
                                                 return entry.ownedTexture || entry.ownedBuffer;
                                             }))
        {
            return std::unexpected("Choose native heaps before the first graph compilation");
        }
        const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
        if (!caps || !caps->hasAliasMemoryBarrier || !m_Device.core.CmdAliasMemoryBarrier ||
            !m_Device.core.GetBufferMemoryDesc || !m_Device.core.GetTextureMemoryDesc ||
            !m_Device.core.AllocateMemory || !m_Device.core.FreeMemory || !m_Device.core.BindBufferMemory ||
            !m_Device.core.BindTextureMemory)
        {
            return std::unexpected("Native heap compilation requires explicit memory and alias barrier support");
        }
        if (!std::has_single_bit(caps->bufferTextureGranularity))
        {
            return std::unexpected("Native heap packing requires reported buffer/texture granularity");
        }
        const auto alignOffset = [](uint64_t offset, uint64_t alignment) -> std::expected<uint64_t, std::string>
        {
            const auto padding = (uint64_t(0) - offset) & (alignment - 1);
            if (padding > std::numeric_limits<uint64_t>::max() - offset)
            {
                return std::unexpected("Graph heap placement alignment overflows");
            }
            return offset + padding;
        };
        m_Compiled = false;
        if (auto planned = planPassesExpected(); !planned)
        {
            return planned;
        }

        std::optional<RecordingOrder> schedule;
        if (initial)
        {
            auto destinations = passDestinationsExpected(initial, recordings);
            if (!destinations)
            {
                return std::unexpected(destinations.error());
            }
            auto validated = validateRecordingDependenciesExpected(initial, *destinations, submissionOrder, {}, {}, {});
            if (!validated)
            {
                return std::unexpected(validated.error());
            }
            schedule = std::move(*validated);
        }

        struct Candidate
        {
            std::unique_ptr<Texture> texture;
            std::unique_ptr<Buffer>  buffer;
            VriMemoryDesc            requirements {};
            std::vector<size_t>      uses;
            uint32_t                 heap = UINT32_MAX;
            uint64_t                 offset  = 0;
            bool                     aliases = false;
        };

        struct Slot
        {
            VriMemoryDesc         requirements;
            std::vector<uint32_t> resources;
        };

        // Failure releases tentative resource objects before any allocation to which they were bound.
        std::vector<Heap>              heaps;
        std::vector<Candidate>         candidates(m_Resources.size());
        std::vector<Slot>              slots;
        std::vector<bool>              eligible(m_Resources.size(), true);
        std::vector<std::vector<bool>> initialized(m_Resources.size());
        for (size_t i = 0; i < m_Resources.size(); ++i)
        {
            auto& entry    = m_Resources[i];
            entry.firstUse = UINT32_MAX;
            entry.lastUse  = UINT32_MAX;
            eligible[i]      = !entry.imported && !entry.history && !entry.preserve && !isExported(i);
            const auto cells = entry.isTexture ? size_t(entry.textureDesc.mipNum) * entry.textureDesc.layerNum *
                                                     std::popcount(textureAspects(entry.textureDesc.format)) :
                                                 entry.bufferBoundaries.size() - 1;
            initialized[i].resize(cells);
        }
        // Every use participates, including readers of unrelated cells that do not order one another.
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            if (!m_Passes[p].live)
            {
                continue;
            }
            for (const auto& use : m_Passes[p].uses)
            {
                const auto index = use.resource.index;
                auto&      entry = m_Resources[index];
                entry.firstUse   = std::min(entry.firstUse, uint32_t(p));
                entry.lastUse    = uint32_t(p);
                if (candidates[index].uses.empty() || candidates[index].uses.back() != p)
                {
                    candidates[index].uses.push_back(p);
                }
                const auto initialize = [&](size_t cell)
                {
                    if (!initialized[index][cell])
                    {
                        eligible[index]          = eligible[index] && use.discardPrevious;
                        initialized[index][cell] = true;
                    }
                };
                if (!entry.isTexture)
                {
                    const auto range = normalizedBufferRangeExpected(use.bufferRange, &entry.bufferDesc);
                    if (!range)
                    {
                        return std::unexpected(range.error());
                    }
                    const auto begin = std::ranges::lower_bound(entry.bufferBoundaries, range->offset);
                    const auto end   = std::ranges::lower_bound(entry.bufferBoundaries, range->offset + range->size);
                    for (auto cell = begin; cell != end; ++cell)
                    {
                        initialize(size_t(cell - entry.bufferBoundaries.begin()));
                    }
                    continue;
                }
                auto range = normalizedRangeExpected(use.range, entry.isTexture ? &entry.textureDesc : nullptr);
                if (!range)
                {
                    return std::unexpected(range.error());
                }
                const auto aspects = entry.isTexture ? textureAspects(entry.textureDesc.format) : VriImageAspect_Color;
                const auto planeCount = size_t(std::popcount(aspects));
                const auto mipCount   = entry.isTexture ? entry.textureDesc.mipNum : 1;
                size_t     plane      = 0;
                for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    if (!(aspects & aspect))
                    {
                        continue;
                    }
                    if (!entry.isTexture || (range->aspect & aspect))
                    {
                        for (uint32_t layer = range->baseLayer; layer < range->baseLayer + range->layerNum; ++layer)
                        {
                            for (uint32_t mip = range->baseMip; mip < range->baseMip + range->mipNum; ++mip)
                            {
                                const auto cell = (size_t(layer) * mipCount + mip) * planeCount + plane;
                                initialize(cell);
                            }
                        }
                    }
                    ++plane;
                }
            }
        }
        const auto precedes = [&](uint32_t before, uint32_t after, const RecordingOrder* submission)
        {
            for (const auto a : candidates[before].uses)
            {
                for (const auto z : candidates[after].uses)
                {
                    // Heap activation follows pass recording order even when submissions run on different queues.
                    if (a >= z)
                    {
                        return false;
                    }
                    const bool ordered = submission ? submission->orders(a, z) :
                                                      (m_Passes[z].ancestors[a / 64] & (uint64_t(1) << (a % 64))) != 0;
                    if (!ordered)
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        std::vector<uint32_t> order;
        for (uint32_t i = 0; i < m_Resources.size(); ++i)
        {
            if (m_Resources[i].firstUse != UINT32_MAX || isExported(i))
            {
                order.push_back(i);
            }
        }
        std::ranges::stable_sort(order,
                                 [this](uint32_t a, uint32_t z)
                                 {
                                     return m_Resources[a].firstUse < m_Resources[z].firstUse;
                                 });
        for (const auto i : order)
        {
            auto& entry     = m_Resources[i];
            auto& candidate = candidates[i];
            const auto location  = entry.isTexture ? entry.textureDesc.memoryLocation : entry.bufferDesc.memoryLocation;
            if (entry.imported)
            {
                continue;
            }
            if (entry.isTexture)
            {
                auto desc = entry.textureDesc;
                if (eligible[i])
                {
                    desc.memoryLocation = VriMemoryLocation_Undefined;
                }
                auto texture = Texture::create(m_Device, desc);
                if (!texture)
                {
                    return std::unexpected(entry.name + ": " + texture.error());
                }
                candidate.texture = std::move(*texture);
                if (eligible[i])
                {
                    m_Device.core.GetTextureMemoryDesc(m_Device.handle, &desc, location, &candidate.requirements);
                }
            }
            else
            {
                auto desc = entry.bufferDesc;
                if (eligible[i])
                {
                    desc.memoryLocation = VriMemoryLocation_Undefined;
                }
                auto buffer = Buffer::create(m_Device, desc);
                if (!buffer)
                {
                    return std::unexpected(entry.name + ": " + buffer.error());
                }
                candidate.buffer = std::move(*buffer);
                if (eligible[i])
                {
                    m_Device.core.GetBufferMemoryDesc(m_Device.handle, &desc, location, &candidate.requirements);
                }
            }
            if (!eligible[i])
            {
                continue;
            }
            const auto& required = candidate.requirements;
            if (!required.size || !std::has_single_bit(required.alignment) ||
                required.location == VriMemoryLocation_Undefined)
            {
                return std::unexpected(entry.name + ": invalid native memory requirements");
            }
            if (required.requiresDedicatedAllocation)
            {
                candidate.texture.reset();
                candidate.buffer.reset();
                if (entry.isTexture)
                {
                    auto texture = Texture::create(m_Device, entry.textureDesc);
                    if (!texture)
                    {
                        return std::unexpected(texture.error());
                    }
                    candidate.texture = std::move(*texture);
                }
                else
                {
                    auto buffer = Buffer::create(m_Device, entry.bufferDesc);
                    if (!buffer)
                    {
                        return std::unexpected(buffer.error());
                    }
                    candidate.buffer = std::move(*buffer);
                }
                continue;
            }
            for (uint32_t slot = 0; slot < slots.size(); ++slot)
            {
                auto&      existing = slots[slot];
                const auto a        = existing.requirements.memoryTypeMask;
                const auto z        = required.memoryTypeMask;
                if (existing.requirements.location != required.location || (a && z && !(a & z)))
                {
                    continue;
                }
                std::vector<std::pair<uint64_t, uint64_t>> blocked;
                for (const auto before : existing.resources)
                {
                    // CPU mappings run while recording, before GPU dependencies can order their accesses.
                    if (location == VriMemoryLocation_Device && precedes(before, i, schedule ? &*schedule : nullptr))
                    {
                        continue;
                    }
                    const auto& previous = candidates[before];
                    uint64_t    begin    = previous.offset;
                    uint64_t    end      = begin + previous.requirements.size;
                    if (m_Resources[before].isTexture != entry.isTexture)
                    {
                        // Simultaneously live linear buffers and optimal images cannot share a native page.
                        begin &= ~(caps->bufferTextureGranularity - 1);
                        auto aligned = alignOffset(end, caps->bufferTextureGranularity);
                        if (!aligned)
                        {
                            return std::unexpected(aligned.error());
                        }
                        end = *aligned;
                    }
                    blocked.emplace_back(begin, end);
                }
                std::ranges::sort(blocked);
                uint64_t offset = 0;
                for (const auto& [begin, end] : blocked)
                {
                    if (offset <= begin && required.size <= begin - offset)
                    {
                        break;
                    }
                    if (offset < end)
                    {
                        auto aligned = alignOffset(end, required.alignment);
                        if (!aligned)
                        {
                            return std::unexpected(aligned.error());
                        }
                        offset = *aligned;
                    }
                }
                if (required.size > std::numeric_limits<uint64_t>::max() - offset)
                {
                    return std::unexpected("Graph heap placement size overflows");
                }
                existing.requirements.size           = std::max(existing.requirements.size, offset + required.size);
                existing.requirements.alignment      = std::max(existing.requirements.alignment, required.alignment);
                existing.requirements.memoryTypeMask = a && z ? a & z : a | z;
                existing.resources.push_back(i);
                candidate.heap   = slot;
                candidate.offset = offset;
                break;
            }
            if (candidate.heap == UINT32_MAX)
            {
                candidate.heap = uint32_t(slots.size());
                slots.push_back({required, {i}});
            }
        }
        std::vector<HeapAlias> aliasPairs;
        for (auto& slot : slots)
        {
            auto aligned = alignOffset(slot.requirements.size, slot.requirements.alignment);
            if (!aligned)
            {
                return std::unexpected(aligned.error());
            }
            slot.requirements.size = *aligned;
            for (size_t current = 0; current < slot.resources.size(); ++current)
            {
                const auto i         = slot.resources[current];
                auto& candidate = candidates[i];
                for (size_t prior = 0; prior < current; ++prior)
                {
                    const auto  other       = slot.resources[prior];
                    auto&       previous    = candidates[other];
                    const auto  granularity = m_Resources[i].isTexture == m_Resources[other].isTexture ?
                                                  uint64_t(1) :
                                                  caps->bufferTextureGranularity;
                    if (candidate.offset / granularity <=
                            (previous.offset + previous.requirements.size - 1) / granularity &&
                        previous.offset / granularity <=
                            (candidate.offset + candidate.requirements.size - 1) / granularity)
                    {
                        previous.aliases  = true;
                        candidate.aliases = true;
                        aliasPairs.push_back({other, i, schedule && !precedes(other, i, nullptr)});
                    }
                }
            }
        }
        for (auto& slot : slots)
        {
            VriMemory* raw    = nullptr;
            const auto status = m_Device.core.AllocateMemory(m_Device.handle, &slot.requirements, &raw);
            std::unique_ptr<VriMemory, decltype(VriCoreInterface::FreeMemory)> memory(raw, m_Device.core.FreeMemory);
            if (status != VriResult_Success || !memory)
            {
                return std::unexpected("Allocate graph heap: " + std::to_string(status));
            }
            heaps.push_back({std::move(memory), slot.requirements, std::move(slot.resources)});
        }
        for (const auto i : order)
        {
            auto& candidate = candidates[i];
            auto& entry     = m_Resources[i];
            if (candidate.heap == UINT32_MAX)
            {
                continue;
            }
            auto*      memory = heaps[candidate.heap].memory.get();
            const auto status =
                entry.isTexture ?
                    m_Device.core.BindTextureMemory(m_Device.handle,
                                                    candidate.texture->handle,
                                                    memory,
                                                    candidate.offset) :
                    m_Device.core.BindBufferMemory(m_Device.handle, candidate.buffer->handle, memory, candidate.offset);
            if (status != VriResult_Success)
            {
                return std::unexpected(entry.name + ": bind graph heap: " + std::to_string(status));
            }
            // Publish the requested location, after the private unbound creation/binding transaction succeeds.
            if (entry.isTexture)
            {
                candidate.texture->desc = entry.textureDesc;
            }
            else
            {
                candidate.buffer->desc = entry.bufferDesc;
            }
        }
        std::vector<std::vector<size_t>> submissionUses;
        for (const auto& alias : aliasPairs)
        {
            if (!alias.requiresSubmissionProof)
            {
                continue;
            }
            if (submissionUses.empty())
            {
                submissionUses.resize(m_Resources.size());
            }
            for (const auto resource : {alias.before, alias.after})
            {
                if (submissionUses[resource].empty())
                {
                    submissionUses[resource] = std::move(candidates[resource].uses);
                }
            }
        }
        m_Heaps = std::move(heaps);
        m_HeapAliasPairs = std::move(aliasPairs);
        m_HeapSubmissionUses = std::move(submissionUses);
        for (const auto i : order)
        {
            auto& entry      = m_Resources[i];
            auto& candidate  = candidates[i];
            entry.allocation = i;
            entry.heap       = candidate.heap;
            entry.heapOffset = candidate.offset;
            entry.heapBytes  = candidate.heap == UINT32_MAX ? 0 : candidate.requirements.size;
            if (entry.imported)
            {
                continue;
            }
            entry.ownedTexture = std::move(candidate.texture);
            entry.ownedBuffer  = std::move(candidate.buffer);
            entry.texture      = entry.ownedTexture.get();
            entry.buffer       = entry.ownedBuffer.get();
            m_Device.core.SetDebugName(entry.isTexture ? static_cast<void*>(entry.texture->handle) :
                                                         static_cast<void*>(entry.buffer->handle),
                                       entry.name.c_str());
            if (candidate.aliases)
            {
                m_Passes[entry.firstUse].heapActivations.push_back(i);
            }
        }
        m_Aliased      = true;
        m_HeapAliasing = true;
        m_Compiled     = true;
        ++m_PlanEpoch;
        return {};
    }

    RenderGraph::FamilyOwnership* RenderGraph::trackedFamilies(const Entry& entry)
    {
        if (entry.imported && entry.importOwnership == ImportOwnership::eCallerManaged)
        {
            return nullptr;
        }
        if (entry.texture)
        {
            return &entry.texture->m_GraphFamilies;
        }
        return entry.buffer ? &entry.buffer->m_GraphFamilies : nullptr;
    }

    std::expected<RenderGraph::OwnershipTransfer, std::string>
    RenderGraph::prepareOwnershipTransferExpected(Resource          resource,
                                                  VriCommandBuffer* release,
                                                  VriCommandBuffer* acquire,
                                                  bool              texture)
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        if (m_RecoveryFailed || !m_Compiled || !ownsResource(resource))
        {
            return std::unexpected("Queue-family transfer requires a resource in a compiled, usable graph");
        }
        const auto family = m_Device.core.GetCommandBufferQueueFamilyIndex;
        if (!m_Device.allowsSeparateComputeFamily() || !family || !m_Device.core.CmdQueueFamilyTransfer)
        {
            return std::unexpected("Explicit Vulkan queue-family transfer is unavailable");
        }
        const uint32_t source      = family(m_Device.handle, release);
        const uint32_t destination = family(m_Device.handle, acquire);
        if (source == UINT32_MAX || destination == UINT32_MAX || source == destination)
        {
            return std::unexpected("Queue-family transfer requires distinct families on this device");
        }
        auto& entry     = m_Resources[resource.index];
        auto* ownership = trackedFamilies(entry);
        if (entry.isTexture != texture || !ownership || (!entry.imported && !isExported(resource.index)))
        {
            return std::unexpected("Queue-family transfer requires a tracked import or a retained graph export");
        }
        if (texture ? (entry.textureDesc.usage & VriTextureUsage_ConcurrentQueues) != 0 :
                      (entry.bufferDesc.usage & VriBufferUsage_ConcurrentQueues) != 0)
        {
            return std::unexpected("Concurrent resources do not support exclusive queue-family transfer");
        }
        if ((entry.texture && entry.texture->m_CallbackAccess) || (entry.buffer && entry.buffer->m_CallbackAccess))
        {
            return std::unexpected("Cannot transfer a resource during another graph's recording callback");
        }
        return OwnershipTransfer {entry, *ownership, source, destination};
    }

    std::expected<void, std::string> RenderGraph::transferBufferOwnershipExpected(Resource          resource,
                                                                                  BufferRange       range,
                                                                                  VriCommandBuffer* release,
                                                                                  VriCommandBuffer* acquire,
                                                                                  VriAccessStage    next)
    {
        auto transfer = prepareOwnershipTransferExpected(resource, release, acquire, false);
        if (!transfer)
        {
            return std::unexpected(transfer.error());
        }
        auto& buffer    = *transfer->entry.buffer;
        auto  selection = normalizedBufferRangeExpected(range, &buffer.desc);
        if (!selection)
        {
            return std::unexpected(selection.error());
        }
        if (!transfer->entry.imported && !coversExportedRange({resource, {}, *selection}))
        {
            return std::unexpected("Buffer queue-family transfer range is not fully exported: " + transfer->entry.name);
        }
        const uint64_t end = selection->offset + selection->size;
        if (transfer->ownership.check(selection->offset, end, transfer->source) != FamilyOwnership::Claim::eUnchanged)
        {
            return std::unexpected("Buffer range is not entirely owned by the releasing queue family");
        }
        auto ownership = transfer->ownership;
        ownership.claim(selection->offset, end, buffer.desc.size, transfer->destination);
        if (auto recorded = recordOwnershipTransferExpected({resource, {}, *selection},
                                                            release,
                                                            acquire,
                                                            {next.access, VriLayout_Undefined, next.stages});
            !recorded)
        {
            return recorded;
        }
        transfer->ownership = std::move(ownership);
        return {};
    }

    std::expected<void, std::string> RenderGraph::transferTextureOwnershipExpected(Resource             resource,
                                                                                   TextureRange         range,
                                                                                   VriCommandBuffer*    release,
                                                                                   VriCommandBuffer*    acquire,
                                                                                   VriAccessLayoutStage next)
    {
        auto transfer = prepareOwnershipTransferExpected(resource, release, acquire, true);
        if (!transfer)
        {
            return std::unexpected(transfer.error());
        }
        auto&       texture   = *transfer->entry.texture;
        const auto& desc      = texture.desc;
        auto        selection = normalizedRangeExpected(range, &desc);
        if (!selection)
        {
            return std::unexpected(selection.error());
        }
        if (!transfer->entry.imported && !coversExportedRange({resource, *selection}))
        {
            return std::unexpected("Texture queue-family transfer range is not fully exported: " +
                                   transfer->entry.name);
        }
        const auto     aspects   = textureAspects(desc.format);
        const uint64_t total     = uint64_t(desc.layerNum) * desc.mipNum * std::popcount(aspects);
        auto           ownership = transfer->ownership;
        bool           owned     = true;
        visitTextureCells(desc,
                          *selection,
                          [&](size_t cell)
                          {
                              if (transfer->ownership.check(cell, cell + 1, transfer->source) !=
                                  FamilyOwnership::Claim::eUnchanged)
                              {
                                  owned = false;
                                  return;
                              }
                              ownership.claim(cell, cell + 1, total, transfer->destination);
                          });
        if (!owned)
        {
            return std::unexpected("Texture subresources are not entirely owned by the releasing queue family");
        }

        if (auto recorded = recordOwnershipTransferExpected({resource, *selection}, release, acquire, next); !recorded)
        {
            return recorded;
        }
        transfer->ownership = std::move(ownership);
        return {};
    }

    std::expected<void, std::string> RenderGraph::recordOwnershipTransferExpected(const ResourceRange& selection,
                                                                                  VriCommandBuffer*    release,
                                                                                  VriCommandBuffer*    acquire,
                                                                                  VriAccessLayoutStage next)
    {
        const auto transfer = [&](const VriBarrierGroupDesc& group)
        {
            const auto recorded = m_RecordedDestinations.size();
            noteRecording(release);
            noteRecording(acquire);
            const auto result = m_Device.core.CmdQueueFamilyTransfer(release, acquire, &group);
            if (result != VriResult_Success)
            {
                m_RecordedDestinations.resize(recorded);
            }
            return result;
        };
        auto& entry = m_Resources[selection.resource.index];
        if (!entry.isTexture)
        {
            auto&                buffer = *entry.buffer;
            const auto           range  = selection.bufferRange;
            const VriAccessStage state {next.access, next.stages};
            buffer.prepareBarriers(state, range.offset, range.size, false);
            const VriBarrierGroupDesc barriers {buffer.m_Barriers.data(),
                                                uint32_t(buffer.m_Barriers.size()),
                                                nullptr,
                                                0};
            const auto                result = transfer(barriers);
            if (result != VriResult_Success)
            {
                return std::unexpected("Record buffer queue-family transfer failed: " + std::to_string(result));
            }
            buffer.commitState(state, range.offset, range.size);
            return {};
        }
        auto&                              texture = *entry.texture;
        const auto&                        desc    = texture.desc;
        const auto                         range   = selection.range;
        const auto                         aspects = textureAspects(desc.format);
        const auto                         total   = uint64_t(desc.layerNum) * desc.mipNum * std::popcount(aspects);
        std::vector<VriTextureBarrierDesc> barriers;
        const auto append = [&](VriAccessLayoutStage before, VriImageAspectFlags mask, uint32_t mip, uint32_t layer)
        {
            barriers.push_back({texture.handle, before, next, mask, mip, 1, layer, 1});
        };
        for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerNum; ++layer)
        {
            for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipNum; ++mip)
            {
                if (auto before = texture.stateExpected(mip, layer, range.aspect))
                {
                    append(*before, range.aspect, mip, layer);
                    continue;
                }
                // Mixed depth/stencil scopes need independent barriers for the two planes.
                for (const auto aspect : {VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    auto plane = texture.stateExpected(mip, layer, aspect);
                    if (!plane)
                    {
                        return std::unexpected(plane.error());
                    }
                    append(*plane, aspect, mip, layer);
                }
            }
        }
        const bool partial = range.mipNum != desc.mipNum || range.layerNum != desc.layerNum || range.aspect != aspects;
        if (partial && texture.m_SubresourceStates.empty())
        {
            texture.m_SubresourceStates.reserve(size_t(total));
        }
        const VriBarrierGroupDesc group {nullptr, 0, barriers.data(), uint32_t(barriers.size())};
        const auto                result = transfer(group);
        if (result != VriResult_Success)
        {
            return std::unexpected("Record texture queue-family transfer failed: " + std::to_string(result));
        }
        texture.commitState(next, {range.baseMip, range.mipNum, range.baseLayer, range.layerNum}, range.aspect);
        return {};
    }

    std::expected<RenderGraph::ResourceRange, std::string>
    RenderGraph::normalizeResourceRangeExpected(ResourceRange selection) const
    {
        if (!ownsResource(selection.resource))
        {
            return std::unexpected("Resource range belongs to another graph");
        }
        const auto& entry   = m_Resources[selection.resource.index];
        auto        texture = normalizedRangeExpected(selection.range, entry.isTexture ? &entry.textureDesc : nullptr);
        auto        buffer =
            normalizedBufferRangeExpected(selection.bufferRange, entry.isTexture ? nullptr : &entry.bufferDesc);
        if (!texture || !buffer)
        {
            return std::unexpected(entry.name + ": " + (texture ? buffer.error() : texture.error()));
        }
        return ResourceRange {selection.resource, *texture, *buffer};
    }

    std::expected<std::vector<RenderGraph::PlannedHandoff>, std::string>
    RenderGraph::planHandoffsExpected(std::span<const QueueFamilyHandoff> handoffs,
                                      std::span<VriCommandBuffer* const>  destinations,
                                      std::span<const HistoryClear>       clears) const
    {
        if (handoffs.empty())
        {
            return std::vector<PlannedHandoff> {};
        }
        if (!m_Device.allowsSeparateComputeFamily() || !m_Device.core.GetCommandBufferQueueFamilyIndex ||
            !m_Device.core.CmdQueueFamilyTransfer)
        {
            return std::unexpected("In-graph Vulkan queue-family handoffs are unavailable");
        }
        const auto passIndex = [&](std::string_view name) -> std::expected<size_t, std::string>
        {
            size_t found = SIZE_MAX;
            for (size_t i = 0; i < m_Passes.size(); ++i)
            {
                if (m_Passes[i].live && m_Passes[i].name == name)
                {
                    if (found != SIZE_MAX)
                    {
                        return std::unexpected("Ambiguous active queue-family handoff pass: " + std::string(name));
                    }
                    found = i;
                }
            }
            if (found == SIZE_MAX)
            {
                return std::unexpected("Unknown active queue-family handoff pass: " + std::string(name));
            }
            return found;
        };

        std::vector<PlannedHandoff> result;
        result.reserve(handoffs.size());
        for (const auto& handoff : handoffs)
        {
            auto selection = normalizeResourceRangeExpected(handoff.selection);
            if (!selection)
            {
                return std::unexpected(selection.error());
            }
            auto acquire = passIndex(handoff.acquireBefore);
            if (!acquire)
            {
                return std::unexpected(acquire.error());
            }
            if (handoff.releaseAfter.has_value() == (handoff.releaseAfterReset != nullptr))
            {
                return std::unexpected("Queue-family handoff requires one release pass or history clear Frame");
            }
            const auto& entry      = m_Resources[selection->resource.index];
            const bool  concurrent = entry.isTexture ?
                                         (entry.textureDesc.usage & VriTextureUsage_ConcurrentQueues) != 0 :
                                         (entry.bufferDesc.usage & VriBufferUsage_ConcurrentQueues) != 0;
            if (!trackedFamilies(entry) || concurrent)
            {
                return std::unexpected(entry.name + ": queue-family handoff requires tracked exclusive storage");
            }
            if (entry.isTexture)
            {
                const auto  available = textureAspects(entry.textureDesc.format);
                const auto* caps      = m_Device.core.GetDeviceDesc(m_Device.handle);
                if (selection->range.aspect != available && (!caps || !caps->hasSeparateDepthStencilLayouts))
                {
                    return std::unexpected(entry.name + ": queue-family handoff requires independent aspect layouts");
                }
            }
            const auto coveringUsage = [&](size_t pass) -> std::optional<Usage>
            {
                for (const auto& use : m_Passes[pass].uses)
                {
                    if (use.resource.index != selection->resource.index)
                    {
                        continue;
                    }
                    const auto range = normalizeResourceRangeExpected({use.resource, use.range, use.bufferRange});
                    // Every use in an active pass has already been normalized by compilation.
                    if (range && (entry.isTexture ? covers(range->range, selection->range) :
                                                    covers(range->bufferRange, selection->bufferRange)))
                    {
                        return use.usage;
                    }
                }
                return std::nullopt;
            };
            const auto next = coveringUsage(*acquire);
            if (!next)
            {
                return std::unexpected(entry.name + ": handoff acquire pass must declare the full selected range");
            }

            VriCommandBuffer* release     = nullptr;
            size_t            releasePass = SIZE_MAX;
            if (handoff.releaseAfterReset)
            {
                release             = handoff.releaseAfterReset->recordingDestination();
                auto selectedClears = clears |
                                      std::views::filter(
                                          [&](const HistoryClear& clear)
                                          {
                                              return clear.commands == release &&
                                                     clear.selection.resource.index == selection->resource.index;
                                          }) |
                                      std::views::transform(&HistoryClear::selection);
                if (!coversRanges(entry.isTexture ? &entry.textureDesc : nullptr, *selection, selectedClears))
                {
                    return std::unexpected(entry.name + ": release Frame must clear the full handoff range");
                }
            }
            else
            {
                const auto pass = passIndex(*handoff.releaseAfter);
                if (!pass)
                {
                    return std::unexpected(pass.error());
                }
                if (*pass >= *acquire)
                {
                    return std::unexpected("Queue-family handoff release pass must precede its acquire pass");
                }
                if (!coveringUsage(*pass))
                {
                    return std::unexpected(entry.name + ": handoff release pass must declare the full selected range");
                }
                releasePass = *pass;
                release     = destinations[*pass];
            }
            result.push_back({*selection, release, releasePass, *acquire, stateForUsage(*next)});
        }
        std::ranges::sort(result, {}, &PlannedHandoff::acquirePass);
        return result;
    }

    std::expected<RenderGraph::FamilyPlan, std::string>
    RenderGraph::planRecordingFamiliesExpected(std::span<VriCommandBuffer* const> streams,
                                               std::span<const HistoryClear>      clears,
                                               std::span<const size_t>            passStreams,
                                               std::span<const PlannedHandoff>    handoffs) const
    {
        const auto queueFamily = m_Device.core.GetCommandBufferQueueFamilyIndex;
        const bool retainOwners = m_Device.allowsSeparateComputeFamily();
        if (!queueFamily || (!retainOwners && streams.size() == 1))
        {
            return FamilyPlan {};
        }
        std::vector<uint32_t> families;
        families.reserve(streams.size());
        for (auto* commands : streams)
        {
            const uint32_t family = queueFamily(m_Device.handle, commands);
            if (family == UINT32_MAX)
            {
                return std::unexpected("Graph recording command buffer belongs to another Vulkan device");
            }
            families.push_back(family);
        }
        const uint32_t initial     = families.front();
        const bool     crossFamily = std::ranges::any_of(families,
                                                     [initial](uint32_t family)
                                                     {
                                                         return family != initial;
                                                     });
        if (!retainOwners && !crossFamily)
        {
            return FamilyPlan {};
        }

        // Caller-managed imports participate in this recording without publishing graph claims.
        FamilyPlan plan(m_Resources.size());
        const FamilyOwnership unclaimed;
        const auto            track = [&](const Entry& entry)
        {
            return (crossFamily || trackedFamilies(entry)) &&
                   !(entry.isTexture ? (entry.textureDesc.usage & VriTextureUsage_ConcurrentQueues) :
                                       (entry.bufferDesc.usage & VriBufferUsage_ConcurrentQueues));
        };
        const auto claim = [&](size_t                  index,
                               uint64_t                begin,
                               uint64_t                end,
                               uint64_t                total,
                               uint32_t                family,
                               std::optional<uint32_t> source = std::nullopt)
        {
            const auto&  entry     = m_Resources[index];
            const size_t owner     = entry.allocation == UINT32_MAX ? index : entry.allocation;
            auto&        pending   = plan[owner];
            const auto*  ownership = pending ? &*pending : trackedFamilies(m_Resources[owner]);
            if (!ownership)
            {
                ownership = &unclaimed;
            }
            const auto status = ownership->check(begin, end, source.value_or(family));
            if (source ? status != FamilyOwnership::Claim::eUnchanged : status == FamilyOwnership::Claim::eConflict)
            {
                return false;
            }
            if (source || status == FamilyOwnership::Claim::eUnowned)
            {
                if (!pending)
                {
                    pending = *ownership;
                }
                pending->claim(begin, end, total, family);
            }
            return true;
        };
        const auto familyOf = [&](VriCommandBuffer* commands)
        {
            const auto stream = std::ranges::find(streams, commands);
            return families[size_t(stream - streams.begin())];
        };
        const auto claimRange =
            [&](const ResourceRange& selection, uint32_t family, std::optional<uint32_t> source = std::nullopt)
        {
            const auto  index = selection.resource.index;
            const auto& entry = m_Resources[index];
            if (!entry.isTexture)
            {
                const auto range = selection.bufferRange;
                return claim(index, range.offset, range.offset + range.size, entry.bufferDesc.size, family, source);
            }
            const auto& desc    = entry.textureDesc;
            const auto  range   = selection.range;
            const auto  aspects = textureAspects(desc.format);
            const auto  total   = uint64_t(desc.layerNum) * desc.mipNum * std::popcount(aspects);
            if (range.mipNum == desc.mipNum && range.layerNum == desc.layerNum && range.aspect == aspects)
            {
                return claim(index, 0, total, total, family, source);
            }
            bool valid = true;
            visitTextureCells(desc,
                              range,
                              [&](size_t cell)
                              {
                                  valid = valid && claim(index, cell, cell + 1, total, family, source);
                              });
            return valid;
        };
        const auto rejected = [](const Entry& entry)
        {
            return "exclusive resource range crosses Vulkan queue families: " + entry.name;
        };
        for (const auto& clear : clears)
        {
            const auto& entry = m_Resources[clear.selection.resource.index];
            if (!track(entry))
            {
                continue;
            }
            const auto family = familyOf(clear.commands);
            if (clear.release)
            {
                const auto source = familyOf(clear.release);
                if (source == family)
                {
                    return std::unexpected(entry.name + ": history release requires distinct native families");
                }
                if (!claimRange(clear.selection, family, source))
                {
                    return std::unexpected(entry.name + ": history range is not owned by the releasing queue family");
                }
            }
            else if (!claimRange(clear.selection, family))
            {
                return std::unexpected("Reset graph history: " + rejected(entry));
            }
        }
        size_t handoffIndex = 0;
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            const auto& pass = m_Passes[p];
            if (!pass.live)
            {
                continue;
            }
            const uint32_t family = passStreams.empty() ? initial : families[passStreams[p]];
            while (handoffIndex < handoffs.size() && handoffs[handoffIndex].acquirePass == p)
            {
                const auto& handoff = handoffs[handoffIndex++];
                const auto  source  = familyOf(handoff.release);
                if (source == family)
                {
                    return std::unexpected("Queue-family handoff requires distinct native families");
                }
                if (!claimRange(handoff.selection, family, source))
                {
                    const auto& entry = m_Resources[handoff.selection.resource.index];
                    return std::unexpected(entry.name + ": handoff range is not owned by the releasing queue family");
                }
            }
            for (const auto& use : pass.uses)
            {
                const size_t index = use.resource.index;
                const auto&  entry = m_Resources[index];
                if (!track(entry))
                {
                    continue;
                }
                const auto range = normalizeResourceRangeExpected({use.resource, use.range, use.bufferRange});
                if (!range)
                {
                    return std::unexpected(range.error());
                }
                if (!claimRange(*range, family))
                {
                    return std::unexpected(pass.name + ": " + rejected(entry));
                }
            }
        }
        return plan;
    }

    void RenderGraph::publishRecordingFamilies(FamilyPlan plan)
    {
        if (!m_Device.allowsSeparateComputeFamily())
        {
            return;
        }
        for (size_t i = 0; i < plan.size(); ++i)
        {
            if (auto* stored = trackedFamilies(m_Resources[i]); stored && plan[i])
            {
                *stored = std::move(*plan[i]);
            }
        }
    }

    std::expected<std::vector<VriCommandBuffer*>, std::string>
    RenderGraph::passDestinationsExpected(VriCommandBuffer* initial, std::span<const PassRecording> recordings) const
    {
        std::vector<VriCommandBuffer*> destinations(m_Passes.size(), initial);
        if (!recordings.empty())
        {
            size_t index = 0;
            for (const auto& pass : m_Passes)
            {
                if (!pass.live)
                {
                    continue;
                }
                if (index >= recordings.size() || recordings[index].pass != pass.name || !recordings[index].commands)
                {
                    return std::unexpected("Recording destinations must match every active pass in order");
                }
                destinations[size_t(&pass - m_Passes.data())] = recordings[index].commands;
                ++index;
            }
            if (index != recordings.size())
            {
                return std::unexpected("Recording destinations contain extra passes");
            }
        }
        return destinations;
    }

    std::expected<RenderGraph::RecordingOrder, std::string>
    RenderGraph::validateRecordingDependenciesExpected(VriCommandBuffer*                                   initial,
                                                       std::span<VriCommandBuffer* const>                  destinations,
                                                       std::optional<std::span<const RecordingDependency>> order,
                                                       std::span<const HistoryClear>                       clears,
                                                       std::span<const HistoryResetRecording>              placements,
                                                       std::span<const PlannedHandoff> handoffs) const
    {
        std::vector<VriCommandBuffer*> streams {initial};
        const auto                     addStream = [&](VriCommandBuffer* commands)
        {
            const auto found = std::ranges::find(streams, commands);
            const auto index = size_t(found - streams.begin());
            if (found == streams.end())
            {
                streams.push_back(commands);
            }
            return index;
        };
        std::vector<size_t> passStreams(m_Passes.size(), SIZE_MAX);
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            if (!m_Passes[p].live)
            {
                continue;
            }
            passStreams[p] = addStream(destinations[p]);
        }
        for (const auto& placement : placements)
        {
            addStream(placement.frame->recordingDestination());
            if (placement.release)
            {
                addStream(placement.release->recordingDestination());
            }
        }
        std::vector<std::vector<bool>> required(streams.size(), std::vector<bool>(streams.size()));
        for (const auto& clear : clears)
        {
            if (clear.release)
            {
                const auto from = std::ranges::find(streams, clear.release);
                const auto to   = std::ranges::find(streams, clear.commands);
                required[size_t(from - streams.begin())][size_t(to - streams.begin())] = true;
            }
        }
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            if (!m_Passes[p].live)
            {
                continue;
            }
            const auto to = passStreams[p];
            for (const auto dependency : m_Passes[p].dependencies)
            {
                if (m_Passes[dependency].live && passStreams[dependency] != to)
                {
                    required[passStreams[dependency]][to] = true;
                }
            }
            for (const auto& use : m_Passes[p].uses)
            {
                const auto& entry = m_Resources[use.resource.index];
                if (!entry.historyResetPending)
                {
                    continue;
                }
                auto texture = normalizedRangeExpected(use.range, entry.isTexture ? &entry.textureDesc : nullptr);
                auto buffer =
                    normalizedBufferRangeExpected(use.bufferRange, entry.isTexture ? nullptr : &entry.bufferDesc);
                if (!texture || !buffer)
                {
                    return std::unexpected(texture ? buffer.error() : texture.error());
                }
                // The planner groups clears by resource index.
                const auto resourceIndex = [](const HistoryClear& clear)
                {
                    return clear.selection.resource.index;
                };
                auto clear = std::ranges::lower_bound(clears, use.resource.index, {}, resourceIndex);
                for (; clear != clears.end() && resourceIndex(*clear) == use.resource.index; ++clear)
                {
                    if (clear->commands == streams[to])
                    {
                        continue;
                    }
                    const bool intersects = entry.isTexture ? overlaps(clear->selection.range, *texture) :
                                                              overlaps(clear->selection.bufferRange, *buffer);
                    if (intersects)
                    {
                        const auto from                              = std::ranges::find(streams, clear->commands);
                        required[size_t(from - streams.begin())][to] = true;
                    }
                }
            }
        }
        for (const auto& handoff : handoffs)
        {
            const auto source      = size_t(std::ranges::find(streams, handoff.release) - streams.begin());
            const auto destination = passStreams[handoff.acquirePass];
            if (source == destination)
            {
                return std::unexpected("Queue-family handoff requires distinct recording destinations");
            }
            required[source][destination] = true;
            const auto  index             = handoff.selection.resource.index;
            const auto& entry             = m_Resources[index];
            // Include independent readers on either side of the ownership change.
            for (size_t p = 0; p < m_Passes.size(); ++p)
            {
                const auto& pass = m_Passes[p];
                if (!pass.live)
                {
                    continue;
                }
                for (const auto& use : pass.uses)
                {
                    if (use.resource.index != index)
                    {
                        continue;
                    }
                    auto range = normalizeResourceRangeExpected({use.resource, use.range, use.bufferRange});
                    if (!range)
                    {
                        return std::unexpected(range.error());
                    }
                    if (!(entry.isTexture ? overlaps(range->range, handoff.selection.range) :
                                            overlaps(range->bufferRange, handoff.selection.bufferRange)))
                    {
                        continue;
                    }
                    const bool beforeRelease = handoff.releasePass != SIZE_MAX && p <= handoff.releasePass;
                    if (!beforeRelease && p < handoff.acquirePass)
                    {
                        return std::unexpected(pass.name + ": overlapping use between queue-family handoff endpoints");
                    }
                    const auto stream = passStreams[p];
                    if (beforeRelease && stream != source)
                    {
                        required[stream][source] = true;
                    }
                    if (p >= handoff.acquirePass && stream != destination)
                    {
                        required[destination][stream] = true;
                    }
                }
            }
        }
        std::vector<std::vector<bool>> declared;
        if (order)
        {
            declared.assign(streams.size(), std::vector<bool>(streams.size()));
            for (const auto& edge : *order)
            {
                const auto before = std::ranges::find(streams, edge.before);
                const auto after  = std::ranges::find(streams, edge.after);
                if (before == streams.end() || after == streams.end())
                {
                    return std::unexpected("Recording dependency references an unknown command buffer");
                }
                if (before == after)
                {
                    return std::unexpected("Recording dependency must order distinct command buffers");
                }
                declared[size_t(before - streams.begin())][size_t(after - streams.begin())] = true;
            }
        }
        const auto&         edges = order ? declared : required;
        std::vector<size_t> incoming(streams.size());
        for (const auto& row : edges)
        {
            for (size_t to = 0; to < row.size(); ++to)
            {
                incoming[to] += size_t(row[to]);
            }
        }
        std::vector<size_t> ready;
        for (size_t stream = 0; stream < streams.size(); ++stream)
        {
            if (!incoming[stream])
            {
                ready.push_back(stream);
            }
        }
        std::vector<size_t> sorted;
        sorted.reserve(streams.size());
        while (!ready.empty())
        {
            const auto from = ready.back();
            ready.pop_back();
            sorted.push_back(from);
            for (size_t to = 0; to < streams.size(); ++to)
            {
                if (edges[from][to] && --incoming[to] == 0)
                {
                    ready.push_back(to);
                }
            }
        }
        if (sorted.size() != streams.size())
        {
            return std::unexpected("Recording destinations form a cyclic submission dependency; split command buffers");
        }
        if (!order && !m_HeapAliasPairs.empty() && streams.size() > 1)
        {
            const size_t        split = streams.size();
            std::vector<size_t> resourceStreams(m_Resources.size(), SIZE_MAX);
            for (size_t p = 0; p < m_Passes.size(); ++p)
            {
                if (m_Passes[p].live)
                {
                    for (const auto& use : m_Passes[p].uses)
                    {
                        auto& stream = resourceStreams[use.resource.index];
                        if (stream == SIZE_MAX)
                        {
                            stream = passStreams[p];
                        }
                        else if (stream != passStreams[p])
                        {
                            stream = split;
                        }
                    }
                }
            }
            for (const auto& alias : m_HeapAliasPairs)
            {
                if (resourceStreams[alias.before] == split ||
                    resourceStreams[alias.before] != resourceStreams[alias.after])
                {
                    return std::unexpected("Native heap handoff requires explicit recording order: " +
                                           m_Resources[alias.before].name + " -> " + m_Resources[alias.after].name);
                }
            }
        }
        auto reaches = edges;
        // Downstream destinations already contain their full reachability in reverse topological order.
        for (const auto from : std::views::reverse(sorted))
        {
            for (size_t via = 0; via < streams.size(); ++via)
            {
                if (edges[from][via])
                {
                    for (size_t to = 0; to < streams.size(); ++to)
                    {
                        reaches[from][to] = reaches[from][to] || reaches[via][to];
                    }
                }
            }
            if (order)
            {
                for (size_t to = 0; to < streams.size(); ++to)
                {
                    if (required[from][to] && !reaches[from][to])
                    {
                        return std::unexpected(
                            "Recording order misses a required submission dependency from destination " +
                            std::to_string(from) + " to " + std::to_string(to));
                    }
                }
            }
        }
        RecordingOrder result {std::move(streams), std::move(passStreams), std::move(reaches)};
        for (const auto& alias : m_HeapAliasPairs)
        {
            if (!alias.requiresSubmissionProof)
            {
                continue;
            }
            for (const auto before : m_HeapSubmissionUses[alias.before])
            {
                for (const auto after : m_HeapSubmissionUses[alias.after])
                {
                    if (!result.orders(before, after))
                    {
                        return std::unexpected("Recording order does not prove native heap handoff: " +
                                               m_Resources[alias.before].name + " -> " + m_Resources[alias.after].name);
                    }
                }
            }
        }
        return result;
    }

    std::expected<void, std::string> RenderGraph::trackRoutedLayoutsExpected(const Use&            use,
                                                                             size_t                pass,
                                                                             bool                  afterCallback,
                                                                             const RecordingOrder& order,
                                                                             RoutedTexture&        texture) const
    {
        const auto& entry = m_Resources[use.resource.index];
        if (!entry.isTexture)
        {
            return {};
        }
        auto range = normalizedRangeExpected(use.range, &entry.textureDesc);
        if (!range)
        {
            return std::unexpected(range.error());
        }
        if (texture.cells.empty())
        {
            texture.cells.resize(size_t(entry.textureDesc.mipNum) * entry.textureDesc.layerNum *
                                 std::popcount(textureAspects(entry.textureDesc.format)));
        }
        const auto desired = stateForUsage(use.usage).layout;
        bool       storesDepth = use.usage == Usage::eDepthRead;
        if (use.usage == Usage::eDepthReadOnly)
        {
            // VRI uses STORE_OP_NONE for read-only Vulkan attachments. Metal preserves them with Store.
            const auto* caps = m_Device.core.GetDeviceDesc(m_Device.handle);
            storesDepth      = !caps || caps->graphicsAPI != VriGraphicsAPI_Vulkan;
        }
        const bool                       writesTexture = writes(use.usage) || storesDepth;
        std::expected<void, std::string> result;
        visitTextureCells(
            entry.textureDesc,
            *range,
            [&](size_t index)
            {
                if (!result)
                {
                    return;
                }
                auto&      cell        = texture.cells[index];
                const auto subresource = textureCellRange(entry.textureDesc, index);
                auto       state =
                    entry.texture->stateExpected(subresource.baseMip, subresource.baseLayer, subresource.aspect);
                if (!state)
                {
                    result = std::unexpected(state.error());
                    return;
                }
                const bool changesLayout = state->layout != desired ||
                                           (afterCallback && (texture.callbackCells[index] & Texture::eLayoutChanged));
                if (!afterCallback)
                {
                    if (cell.layoutOwner != SIZE_MAX && !order.orders(cell.layoutOwner, pass))
                    {
                        result =
                            std::unexpected(entry.name + ": layout transition from " + m_Passes[cell.layoutOwner].name +
                                            " is unordered on this recording destination");
                        return;
                    }
                    if (cell.lastWriter != SIZE_MAX && !order.orders(cell.lastWriter, pass))
                    {
                        result = std::unexpected(entry.name + ": texture write from " + m_Passes[cell.lastWriter].name +
                                                 " is unordered on this recording destination");
                        return;
                    }
                }
                if (changesLayout || (!afterCallback && writesTexture))
                {
                    // A layout transition also writes the native subresource. It must wait for every
                    // overlapping access, including readers that reused the previously established layout.
                    for (const auto& access : texture.accesses)
                    {
                        const auto& used = access.range;
                        if ((used.aspect & subresource.aspect) && subresource.baseMip >= used.baseMip &&
                            subresource.baseMip < used.baseMip + used.mipNum &&
                            subresource.baseLayer >= used.baseLayer &&
                            subresource.baseLayer < used.baseLayer + used.layerNum && !order.orders(access.pass, pass))
                        {
                            result = std::unexpected(
                                entry.name + (changesLayout ? ": layout transition" : ": texture write") +
                                " conflicts with unordered access from " + m_Passes[access.pass].name);
                            return;
                        }
                    }
                }
                if (changesLayout)
                {
                    cell.layoutOwner = pass;
                }
                if (!afterCallback && writesTexture)
                {
                    cell.lastWriter = pass;
                }
                if (afterCallback)
                {
                    texture.callbackCells[index] &= uint8_t(~Texture::eLayoutChanged);
                }
            });
        if (result && afterCallback)
        {
            texture.accesses.push_back({pass, *range});
        }
        return result;
    }

    std::expected<void, std::string>
    RenderGraph::executeRoutedPassExpected(VriCommandBuffer* commands,
                                           size_t passIndex,
                                           const RecordingOrder& order,
                                           RoutedResources& resources)
    {
        struct CaptureScope
        {
            std::span<Entry> entries;
            RoutedResources& resources;

            ~CaptureScope()
            {
                for (const auto index : resources.captured)
                {
                    const auto& entry = entries[index];
                    if (entry.isTexture)
                    {
                        resources.textures[index].callbackAccess.cells = {};
                    }
                    else
                    {
                        auto& buffer = resources.buffers[index];
                        buffer.access.ranges = {};
                        buffer.ranges.clear();
                    }
                }
                resources.captured.clear();
                resources.callback = {};
            }
        } captureScope {m_Resources, resources};

        const auto& pass = m_Passes[passIndex];
        for (const auto& use : pass.uses)
        {
            const auto index = use.resource.index;
            auto& entry = m_Resources[index];
            if (entry.isTexture)
            {
                auto& texture = resources.textures[index];
                auto& cells   = texture.callbackAccess.cells;
                if (cells.empty())
                {
                    texture.callbackCells.assign(texture.cells.size(), uint8_t(0));
                    resources.captured.push_back(index);
                    cells = texture.callbackCells;
                }
                const auto range = normalizedRangeExpected(use.range, &entry.textureDesc);
                if (!range)
                {
                    return std::unexpected(range.error());
                }
                auto permission = uint8_t(Texture::eDeclared);
                if (writes(use.usage))
                {
                    permission |= Texture::eWritable;
                }
                else if (use.usage == Usage::eDepthRead)
                {
                    permission |= Texture::ePreservingStore;
                }
                visitTextureCells(entry.textureDesc,
                                  *range,
                                  [&](size_t cell)
                                  {
                                      cells[cell] |= permission;
                                  });
                continue;
            }

            auto& buffer = resources.buffers[index];
            if (buffer.ranges.empty())
            {
                resources.captured.push_back(index);
            }
            const auto bytes = normalizedBufferRangeExpected(use.bufferRange, &entry.bufferDesc);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            buffer.ranges.push_back({bytes->offset, bytes->size, writes(use.usage)});
        }

        for (const auto index : resources.captured)
        {
            if (!m_Resources[index].isTexture)
            {
                auto& buffer = resources.buffers[index];
                std::ranges::sort(buffer.ranges, {}, &Buffer::CallbackRange::offset);
                buffer.access.ranges = buffer.ranges;
            }
        }
        auto& callback    = resources.callback;
        callback.commands = commands;
        auto executed = pass.executeExpected(commands, *this);
        callback.commands = nullptr;
        if (!callback.error.empty())
        {
            return std::unexpected(m_Resources[callback.failedResource].name + ": " + std::string(callback.error));
        }
        if (!executed)
        {
            return executed;
        }
        for (const auto& use : pass.uses)
        {
            if (auto tracked = trackRoutedLayoutsExpected(use, passIndex, true, order, resources.textures[use.resource.index]);
                !tracked)
            {
                return tracked;
            }
        }
        return {};
    }

    std::expected<void, std::string>
    RenderGraph::preparePassMemoryExpected(VriCommandBuffer* commands, size_t pass, bool routed)
    {
        const auto& activations = m_Passes[pass].heapActivations;
        if (activations.empty() && !routed)
        {
            return {};
        }
        if (!m_Device.core.CmdAliasMemoryBarrier)
        {
            return std::unexpected("Native alias memory barrier is unavailable");
        }
        const auto status = m_Device.core.CmdAliasMemoryBarrier(commands);
        if (status != VriResult_Success)
        {
            return std::unexpected("Activate graph heap: " + std::to_string(status));
        }
        for (const auto index : activations)
        {
            auto& entry = m_Resources[index];
            if (entry.isTexture)
            {
                entry.texture->setState({VriAccess_None, VriLayout_Undefined, VriPipelineStage_AllCommands});
            }
            else
            {
                entry.buffer->setState({VriAccess_None, VriPipelineStage_AllCommands});
            }
        }
        return {};
    }

    bool RenderGraph::aliasesTransients() const
    {
        return m_Aliased;
    }

    bool RenderGraph::supportsRoutedRecording() const
    {
        return !m_Aliased || m_HeapAliasing;
    }

    std::expected<std::vector<RenderGraph::HistoryClear>, std::string>
    RenderGraph::planHistoryClearsExpected(VriCommandBuffer*                      initial,
                                           std::span<const HistoryResetRecording> placements) const
    {
        std::vector<HistoryClear> routes;
        routes.reserve(placements.size());
        const auto* caps = placements.empty() ? nullptr : m_Device.core.GetDeviceDesc(m_Device.handle);
        for (const auto& placement : placements)
        {
            const auto resource = placement.selection.resource;
            if (!ownsResource(resource) || !m_Resources[resource.index].history)
            {
                return std::unexpected("History clear placement requires a history owned by this graph");
            }
            const auto* frame = placement.frame;
            if (!frame || &frame->m_Device != &m_Device || !frame->recordingDestination())
            {
                return std::unexpected("History clear placement requires a Frame on this device");
            }
            const auto& entry = m_Resources[resource.index];
            VriCommandBuffer* release = nullptr;
            if (const auto* source = placement.release)
            {
                if (&source->m_Device != &m_Device || !source->recordingDestination() || source == frame)
                {
                    return std::unexpected(entry.name + ": history release requires a separate Frame on this device");
                }
                const bool concurrent = entry.isTexture ?
                                            (entry.textureDesc.usage & VriTextureUsage_ConcurrentQueues) != 0 :
                                            (entry.bufferDesc.usage & VriBufferUsage_ConcurrentQueues) != 0;
                if (concurrent || !m_Device.allowsSeparateComputeFamily() ||
                    !m_Device.core.GetCommandBufferQueueFamilyIndex || !m_Device.core.CmdQueueFamilyTransfer)
                {
                    return std::unexpected(entry.name + ": history release requires tracked exclusive Vulkan storage");
                }
                release = source->recordingDestination();
            }
            auto        texture =
                normalizedRangeExpected(placement.selection.range, entry.isTexture ? &entry.textureDesc : nullptr);
            auto buffer = normalizedBufferRangeExpected(placement.selection.bufferRange,
                                                        entry.isTexture ? nullptr : &entry.bufferDesc);
            if (!texture || !buffer)
            {
                return std::unexpected(entry.name + ": " + (texture ? buffer.error() : texture.error()));
            }
            if (entry.isTexture)
            {
                const auto aspects = textureAspects(entry.textureDesc.format);
                if (aspects != VriImageAspect_Color && frame->queueType() != VriQueueType_Graphics)
                {
                    return std::unexpected(entry.name + ": depth/stencil history requires a graphics Frame");
                }
                if (frame->queueType() == VriQueueType_Transfer)
                {
                    return std::unexpected(entry.name + ": color history requires a graphics or compute Frame");
                }
                if (texture->aspect != aspects && (!caps || !caps->hasSeparateDepthStencilLayouts))
                {
                    return std::unexpected(entry.name + ": history placement requires independent aspect layouts");
                }
            }
            else if ((buffer->offset || buffer->size != entry.bufferDesc.size) &&
                     (!caps || !caps->hasBufferRangeBarriers || !m_Device.core.CmdBufferBarrierRange))
            {
                return std::unexpected(entry.name + ": history placement requires native byte-range barriers");
            }
            if (std::ranges::any_of(routes,
                                    [&](const HistoryClear& route)
                                    {
                                        return route.selection.resource.index == resource.index &&
                                               (entry.isTexture ? overlaps(route.selection.range, *texture) :
                                                                  overlaps(route.selection.bufferRange, *buffer));
                                    }))
            {
                return std::unexpected(entry.name + ": overlapping history clear placements");
            }
            routes.push_back({{resource, *texture, *buffer}, frame->recordingDestination(), release});
        }
        std::ranges::sort(routes,
                          {},
                          [](const HistoryClear& route)
                          {
                              return std::pair {route.selection.resource.index, route.selection.bufferRange.offset};
                          });

        std::vector<HistoryClear> clears;
        for (uint32_t index = 0; index < m_Resources.size(); ++index)
        {
            const auto& entry = m_Resources[index];
            if (!entry.historyResetPending || (!entry.texture && !entry.buffer))
            {
                continue;
            }
            const Resource                      resource {this, index};
            const auto                          group = std::ranges::equal_range(routes,
                                                        index,
                                                                                 {},
                                                        [](const HistoryClear& route)
                                                        {
                                                            return route.selection.resource.index;
                                                        });
            const std::span<const HistoryClear> selected(group.begin(), group.end());
            if (!entry.isTexture)
            {
                for (const auto& history : entry.bufferHistory)
                {
                    if (!history.state.resetPending)
                    {
                        continue;
                    }
                    auto       cursor = history.range.offset;
                    const auto end    = cursor + history.range.size;
                    for (const auto& route : selected)
                    {
                        const auto range = route.selection.bufferRange;
                        const auto begin = std::max(cursor, range.offset);
                        const auto limit = std::min(end, range.offset + range.size);
                        if (begin >= limit)
                        {
                            continue;
                        }
                        if (cursor < begin)
                        {
                            clears.push_back({{resource, {}, {cursor, begin - cursor}}, initial});
                        }
                        clears.push_back({{resource, {}, {begin, limit - begin}}, route.commands, route.release});
                        cursor = limit;
                    }
                    if (cursor < end)
                    {
                        clears.push_back({{resource, {}, {cursor, end - cursor}}, initial});
                    }
                }
                continue;
            }

            const auto&        desc    = entry.textureDesc;
            const auto         aspects = textureAspects(desc.format);
            const TextureRange whole {0, desc.mipNum, 0, desc.layerNum, aspects};
            if ((selected.empty() || (selected.size() == 1 && selected.front().selection.range == whole)) &&
                std::ranges::all_of(entry.historyCells, &HistoryInfo::resetPending))
            {
                clears.push_back({{resource, whole},
                                  selected.empty() ? initial : selected.front().commands,
                                  selected.empty() ? nullptr : selected.front().release});
                continue;
            }
            using Destination = std::pair<VriCommandBuffer*, VriCommandBuffer*>;
            const Destination        defaultDestination {initial, nullptr};
            std::vector<Destination> destinations {defaultDestination};
            for (const auto& route : selected)
            {
                const Destination destination {route.commands, route.release};
                if (std::ranges::find(destinations, destination) == destinations.end())
                {
                    destinations.push_back(destination);
                }
            }
            const auto planes = size_t(std::popcount(aspects));
            const auto mask   = [&](uint32_t mip, uint32_t layer, Destination destination)
            {
                VriImageAspectFlags result = VriImageAspect_None;
                size_t              cell   = (size_t(layer) * desc.mipNum + mip) * planes;
                for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    if (!(aspects & aspect))
                    {
                        continue;
                    }
                    if (entry.historyCells[cell++].resetPending)
                    {
                        const TextureRange range {mip, 1, layer, 1, aspect};
                        const auto         route = std::ranges::find_if(selected,
                                                                [&](const HistoryClear& candidate)
                                                                {
                                                                    return overlaps(candidate.selection.range, range);
                                                                });
                        const auto         target = route == selected.end() ? defaultDestination :
                                                                              Destination {route->commands, route->release};
                        if (target == destination)
                        {
                            result |= aspect;
                        }
                    }
                }
                return result;
            };
            for (const auto& destination : destinations)
            {
                for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
                {
                    uint32_t mip = 0;
                    while (mip < desc.mipNum)
                    {
                        const auto first           = mip++;
                        const auto selectedAspects = mask(first, layer, destination);
                        if (!selectedAspects)
                        {
                            continue;
                        }
                        while (mip < desc.mipNum && mask(mip, layer, destination) == selectedAspects)
                        {
                            ++mip;
                        }
                        clears.push_back({{resource, {first, mip - first, layer, 1, selectedAspects}},
                                          destination.first,
                                          destination.second});
                    }
                }
            }
        }
        return clears;
    }

    std::expected<void, std::string> RenderGraph::recordHistoryClearsExpected(std::span<const HistoryClear> clears,
                                                                              bool                          routed)
    {
        for (const auto& clear : clears)
        {
            auto& entry    = m_Resources[clear.selection.resource.index];
            auto* commands = clear.commands;
            if (clear.release)
            {
                const auto state = stateForUsage(Usage::eCopyDestination);
                if (auto moved = recordOwnershipTransferExpected(clear.selection, clear.release, commands, state);
                    !moved)
                {
                    return std::unexpected(entry.name + ": acquire history for clear: " + moved.error());
                }
            }
            noteRecording(commands);
            m_Device.core.CmdBeginDebugGroup(commands, "Reset graph history");
            std::expected<void, std::string> recorded;
            if (entry.isTexture)
            {
                const auto&                      selection = clear.selection.range;
                const VriTextureSubresourceRange range {selection.baseMip,
                                                        selection.mipNum,
                                                        selection.baseLayer,
                                                        selection.layerNum};
                recorded = selection.aspect == VriImageAspect_Color ?
                               entry.texture->clearStorageExpected(commands, entry.initial.color, range, routed) :
                               entry.texture->clearDepthStencilExpected(commands,
                                                                        entry.initial.depthStencil,
                                                                        range,
                                                                        selection.aspect,
                                                                        routed);
            }
            else
            {
                const auto range = clear.selection.bufferRange;
                recorded         = entry.buffer->transitionExpected(commands,
                                                                    {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer},
                                                            range.offset,
                                                            range.size,
                                                            routed);
                if (recorded)
                {
                    const uint64_t initialSize  = entry.historyInitialBuffer->desc.size;
                    const bool     repeatedZero = initialSize < entry.bufferDesc.size;
                    const uint64_t end          = range.offset + range.size;
                    for (auto offset = range.offset; offset < end;)
                    {
                        const auto              bytes = std::min(end - offset, initialSize);
                        const VriBufferCopyDesc copy {repeatedZero ? 0 : offset, offset, bytes};
                        m_Device.core.CmdCopyBuffer(commands,
                                                    entry.buffer->handle,
                                                    entry.historyInitialBuffer->handle,
                                                    &copy);
                        offset += bytes;
                    }
                }
            }
            m_Device.core.CmdEndDebugGroup(commands);
            if (!recorded)
            {
                return std::unexpected(entry.name + ": " + recorded.error());
            }
        }
        // Each allocated pending range belongs to exactly one clear in the plan.
        for (const auto& clear : clears)
        {
            auto& entry = m_Resources[clear.selection.resource.index];
            if (!entry.historyResetPending)
            {
                continue;
            }
            for (auto& cell : entry.historyCells)
            {
                cell.resetPending = false;
            }
            for (auto& interval : entry.bufferHistory)
            {
                interval.state.resetPending = false;
            }
            entry.historyResetPending = false;
        }
        return {};
    }

    void RenderGraph::execute(VriCommandBuffer* cmd, Profiler* profiler)
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            throw std::logic_error(idle.error()); // Legacy boundary.
        }
        const GraphStateScope transaction(m_StateTransactionActive);
        if (!m_Compiled)
        {
            throw std::logic_error("Compile the graph before Execute");
        }
        const std::array streams {cmd};
        auto             clears = planHistoryClearsExpected(cmd, {});
        auto families = clears ? planRecordingFamiliesExpected(streams, *clears) : std::unexpected(clears.error());
        if (!families)
        {
            throw std::logic_error(families.error()); // Legacy boundary.
        }
        if (profiler)
        {
            noteRecording(cmd);
            profiler->beginFrame(cmd);
        }
        if (auto reset = recordHistoryClearsExpected(*clears, false); !reset)
        {
            throw std::runtime_error(reset.error()); // Legacy boundary.
        }
        for (const auto& pass : m_Passes)
        {
            if (pass.live)
            {
                noteRecording(cmd);
                m_Device.core.CmdBeginDebugGroup(cmd, pass.name.c_str());
                if (profiler)
                {
                    profiler->beginPass(cmd, pass.name);
                }
                if (auto activated = preparePassMemoryExpected(cmd, size_t(&pass - m_Passes.data())); !activated)
                {
                    check(VriResult_Failure, activated.error().c_str()); // Legacy boundary.
                }
                for (const auto& use : pass.uses)
                {
                    auto&      entry = lookup(use.resource);
                    const auto state = stateForUsage(use.usage);
                    if (entry.isTexture)
                    {
                        auto transitioned =
                            entry.texture->transitionExpected(cmd, state, transitionView(*entry.texture, use.range));
                        check(transitioned ? VriResult_Success : VriResult_Failure,
                              transitioned ? "Transition graph texture" : transitioned.error().c_str());
                    }
                    else
                    {
                        entry.buffer->transition(cmd,
                                                 {state.access, state.stages},
                                                 use.bufferRange.offset,
                                                 use.bufferRange.size);
                    }
                }
                if (profiler)
                {
                    profiler->beginCommands(cmd);
                }
                if (pass.executeExpected)
                {
                    auto result = pass.executeExpected(cmd, *this);
                    check(result ? VriResult_Success : VriResult_Failure,
                          result ? "Execute graph pass" : result.error().c_str()); // Legacy interface.
                }
                else
                {
                    pass.execute(cmd, *this);
                }
                if (profiler)
                {
                    profiler->endPass(cmd);
                }
                m_Device.core.CmdEndDebugGroup(cmd);
            }
        }
        if (profiler)
        {
            profiler->resolve(cmd);
        }
        publishRecordingFamilies(std::move(*families));
    }

    RenderGraph::~RenderGraph()
    {
        for (auto* checkpoint : m_RecordingGuards)
        {
            checkpoint->graph = nullptr;
            checkpoint->firstRecording.reset();
        }
    }

    RenderGraph::StateCheckpoint::StateCheckpoint(StateCheckpoint&& other) noexcept
    {
        *this = std::move(other);
    }

    RenderGraph::StateCheckpoint& RenderGraph::StateCheckpoint::operator=(StateCheckpoint&& other) noexcept
    {
        if (this != &other)
        {
            releaseRecordingGuard();
            graph          = std::exchange(other.graph, nullptr);
            resources      = std::move(other.resources);
            passStates     = std::move(other.passStates);
            recordings     = std::move(other.recordings);
            firstRecording = std::exchange(other.firstRecording, std::nullopt);
            planEpoch      = other.planEpoch;
            historyEpoch   = other.historyEpoch;
            bindingEpoch   = other.bindingEpoch;
            if (firstRecording)
            {
                *std::ranges::find(graph->m_RecordingGuards, &other) = this;
            }
        }
        return *this;
    }

    RenderGraph::StateCheckpoint::~StateCheckpoint()
    {
        releaseRecordingGuard();
    }

    void RenderGraph::StateCheckpoint::releaseRecordingGuard() noexcept
    {
        if (firstRecording)
        {
            std::erase(graph->m_RecordingGuards, this);
            firstRecording.reset();
            if (graph->m_RecordingGuards.empty())
            {
                graph->m_RecordedDestinations.clear();
            }
        }
    }

    void RenderGraph::noteRecording(VriCommandBuffer* commands)
    {
        if (m_RecordingGuards.empty())
        {
            return;
        }
        // One entry per destination since the newest snapshot is sufficient for every live guard.
        const auto destinations = std::span(m_RecordedDestinations).subspan(*m_RecordingGuards.back()->firstRecording);
        if (std::ranges::find(destinations, commands) == destinations.end())
        {
            m_RecordedDestinations.push_back(commands);
        }
    }

    std::expected<RenderGraph::StateCheckpoint, std::string> RenderGraph::checkpointExpected() const
    {
        return checkpointExpected({});
    }

    std::expected<RenderGraph::StateCheckpoint, std::string>
    RenderGraph::checkpointExpected(std::span<const Frame* const> frames) const
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        const GraphStateScope transaction(m_StateTransactionActive);
        std::vector<StateCheckpoint::FrameRecording> recordings;
        recordings.reserve(frames.size());
        for (const auto* frame : frames)
        {
            if (!frame || &frame->m_Device != &m_Device || frame->m_State != Frame::State::eRecording ||
                !frame->m_Device.core.ResetCommandAllocatorChecked)
            {
                return std::unexpected("Graph checkpoint requires recording frames with checked reset on this device");
            }
            if (std::ranges::find(recordings, frame, &StateCheckpoint::FrameRecording::frame) != recordings.end())
            {
                return std::unexpected("Graph checkpoint lists a frame more than once");
            }
            recordings.push_back({frame, frame->m_RecordingSerial});
        }
        auto checkpoint = captureCheckpointExpected();
        if (checkpoint && !recordings.empty())
        {
            checkpoint->recordings = std::move(recordings);
            m_RecordingGuards.push_back(&*checkpoint);
            checkpoint->firstRecording = m_RecordedDestinations.size();
        }
        return checkpoint;
    }

    std::expected<RenderGraph::StateCheckpoint, std::string> RenderGraph::captureCheckpointExpected() const
    {
        if (m_RecoveryFailed)
        {
            return std::unexpected("Rebuild the graph after failed pass-state recovery");
        }

        if (!m_Compiled)
        {
            return std::unexpected("Compile the graph before capturing resource states");
        }
        StateCheckpoint checkpoint;
        checkpoint.graph        = this;
        checkpoint.historyEpoch = m_HistoryEpoch;
        checkpoint.bindingEpoch = m_BindingEpoch;
        checkpoint.planEpoch    = m_PlanEpoch;
        const auto stateCount   = std::ranges::count_if(m_Passes,
                                                      [](const auto& pass)
                                                      {
                                                          return pass.live && bool(pass.captureState);
                                                      });
        checkpoint.passStates.reserve(size_t(stateCount));
        for (const auto& pass : m_Passes)
        {
            if (pass.live && pass.captureState)
            {
                auto state = pass.captureState();
                if (!state)
                {
                    return std::unexpected(pass.name + ": capture CPU state failed: " + state.error());
                }
                if (!*state)
                {
                    return std::unexpected(pass.name + ": CPU state snapshot has no restoration action");
                }
                checkpoint.passStates.push_back({pass.name, std::move(*state)});
            }
        }
        checkpoint.resources.reserve(m_Resources.size());
        for (const auto& entry : m_Resources)
        {
            StateCheckpoint::State state;
            state.texture             = entry.texture;
            state.buffer              = entry.buffer;
            if (const auto* ownership = trackedFamilies(entry))
            {
                state.familyOwnership = *ownership;
            }
            state.historyResetPending = entry.historyResetPending;
            if (entry.historyResetPending && !std::ranges::all_of(entry.historyCells, &HistoryInfo::resetPending))
            {
                state.historyPendingCells.reserve(entry.historyCells.size());
                for (const auto& cell : entry.historyCells)
                {
                    state.historyPendingCells.push_back(cell.resetPending);
                }
            }
            if (entry.historyResetPending && !std::ranges::all_of(entry.bufferHistory,
                                                                  [](const auto& current)
                                                                  {
                                                                      return current.state.resetPending;
                                                                  }))
            {
                state.historyPendingBufferRanges.reserve(entry.bufferHistory.size());
                for (const auto& current : entry.bufferHistory)
                {
                    state.historyPendingBufferRanges.push_back(current.state.resetPending);
                }
            }
            if (state.texture)
            {
                state.textureState = state.texture->captureState();
            }
            if (state.buffer)
            {
                state.bufferState = state.buffer->captureState();
            }
            checkpoint.resources.push_back(std::move(state));
        }
        return checkpoint;
    }

    std::expected<void, std::string> RenderGraph::restoreUnsubmittedExpected(StateCheckpoint&& checkpoint)
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        const GraphStateScope transaction(m_StateTransactionActive);
        return restoreCheckpointExpected(std::move(checkpoint));
    }

    std::expected<void, std::string>
    RenderGraph::validateCheckpointRestoreExpected(const StateCheckpoint& checkpoint) const
    {
        if (checkpoint.graph != this || !m_Compiled || checkpoint.resources.size() != m_Resources.size() ||
            checkpoint.historyEpoch != m_HistoryEpoch || checkpoint.bindingEpoch != m_BindingEpoch ||
            checkpoint.planEpoch != m_PlanEpoch)
        {
            return std::unexpected("Graph changed since the resource-state checkpoint");
        }
        if (checkpoint.firstRecording)
        {
            const auto destinations = std::span(m_RecordedDestinations).subspan(*checkpoint.firstRecording);
            for (const auto* commands : destinations)
            {
                if (std::ranges::none_of(checkpoint.recordings,
                                         [commands](const auto& recording)
                                         {
                                             return recording.frame->recordingDestination() == commands;
                                         }))
                {
                    return std::unexpected("Graph checkpoint omits a recorded command buffer");
                }
            }
        }
        for (const auto& recording : checkpoint.recordings)
        {
            if (recording.frame->m_RecordingSerial != recording.serial ||
                recording.frame->m_State != Frame::State::eIdle || !recording.frame->m_RecordingDiscarded)
            {
                return std::unexpected("Graph checkpoint requires every captured recording to be discarded");
            }
        }
        for (size_t i = 0; i < m_Resources.size(); ++i)
        {
            if (checkpoint.resources[i].texture != m_Resources[i].texture ||
                checkpoint.resources[i].buffer != m_Resources[i].buffer)
            {
                return std::unexpected("Graph resources changed since the resource-state checkpoint");
            }
            const auto& saved = checkpoint.resources[i];
            if ((!saved.historyPendingBufferRanges.empty() &&
                 saved.historyPendingBufferRanges.size() != m_Resources[i].bufferHistory.size()) ||
                (!saved.historyPendingCells.empty() &&
                 saved.historyPendingCells.size() != m_Resources[i].historyCells.size()))
            {
                return std::unexpected("Graph history selection changed since the resource-state checkpoint");
            }
            if (saved.texture && !saved.textureState.belongsTo(*saved.texture))
            {
                return std::unexpected("Graph texture changed since the resource-state checkpoint");
            }
            if (saved.buffer && !saved.bufferState.belongsTo(*saved.buffer))
            {
                return std::unexpected("Graph buffer changed since the resource-state checkpoint");
            }
        }
        return {};
    }

    std::expected<void, std::string> RenderGraph::restoreCheckpointExpected(StateCheckpoint&& checkpoint)
    {
        if (auto valid = validateCheckpointRestoreExpected(checkpoint); !valid)
        {
            return valid;
        }
        for (size_t i = 0; i < checkpoint.resources.size(); ++i)
        {
            auto& state = checkpoint.resources[i];
            if (state.texture)
            {
                if (auto restored = state.texture->restoreUnsubmittedExpected(std::move(state.textureState)); !restored)
                {
                    return restored;
                }
            }
            if (state.buffer)
            {
                if (auto restored = state.buffer->restoreUnsubmittedExpected(std::move(state.bufferState)); !restored)
                {
                    return restored;
                }
            }
            if (auto* ownership = trackedFamilies(m_Resources[i]))
            {
                *ownership = std::move(state.familyOwnership);
            }
            m_Resources[i].historyResetPending = state.historyResetPending;
            auto& cells                        = m_Resources[i].historyCells;
            for (size_t cell = 0; cell < cells.size(); ++cell)
            {
                cells[cell].resetPending =
                    state.historyPendingCells.empty() ? state.historyResetPending : state.historyPendingCells[cell];
            }
            auto& ranges = m_Resources[i].bufferHistory;
            for (size_t range = 0; range < ranges.size(); ++range)
            {
                ranges[range].state.resetPending = state.historyPendingBufferRanges.empty() ?
                                                       state.historyResetPending :
                                                       state.historyPendingBufferRanges[range];
            }
        }
        std::string recoveryError;
        for (auto state = checkpoint.passStates.rbegin(); state != checkpoint.passStates.rend(); ++state)
        {
            if (auto restored = state->restore(); !restored)
            {
                if (!recoveryError.empty())
                {
                    recoveryError += "; ";
                }
                recoveryError += state->pass + ": " + restored.error();
            }
        }
        checkpoint.passStates.clear();
        if (checkpoint.firstRecording)
        {
            const auto restored = *checkpoint.firstRecording;
            // A later snapshot may contain CPU state from recordings we just undid.
            std::erase_if(m_RecordingGuards,
                          [restored](StateCheckpoint* guard)
                          {
                              if (*guard->firstRecording <= restored)
                              {
                                  return false;
                              }
                              guard->firstRecording.reset();
                              guard->graph = nullptr;
                              return true;
                          });
            m_RecordedDestinations.resize(restored);
            checkpoint.releaseRecordingGuard();
        }
        checkpoint.graph = nullptr;
        if (!recoveryError.empty())
        {
            m_RecoveryFailed = true;
            m_Compiled       = false;
            return std::unexpected("Pass-state recovery failed: " + recoveryError);
        }
        return {};
    }

    std::expected<void, std::string> RenderGraph::executeExpected(VriCommandBuffer* cmd, Profiler* profiler)
    {
        return executeExpected(cmd, {}, profiler);
    }

    std::expected<void, std::string>
    RenderGraph::executeExpected(VriCommandBuffer* cmd, std::span<const PassRecording> recordings, Profiler* profiler)
    {
        return executeRecordingsExpected(cmd, recordings, std::nullopt, profiler);
    }

    std::expected<void, std::string>
    RenderGraph::executeOrderedExpected(VriCommandBuffer*                      cmd,
                                        std::span<const PassRecording>         recordings,
                                        std::span<const RecordingDependency>   order,
                                        Profiler*                              profiler,
                                        std::span<const HistoryResetRecording> historyResets,
                                        std::span<const QueueFamilyHandoff>    handoffs)
    {
        return executeRecordingsExpected(cmd, recordings, order, profiler, historyResets, handoffs);
    }

    std::expected<void, std::string>
    RenderGraph::validateOrderedRecordingExpected(VriCommandBuffer*                      initial,
                                                  std::span<const PassRecording>         recordings,
                                                  std::span<const RecordingDependency>   order,
                                                  std::span<const HistoryResetRecording> historyResets,
                                                  std::span<const QueueFamilyHandoff>    handoffs) const
    {
        if (auto prepared = prepareRecordingExpected(initial, recordings, order, nullptr, historyResets, handoffs);
            !prepared)
        {
            return std::unexpected(prepared.error());
        }
        return {};
    }

    std::expected<RenderGraph::PreparedRecording, std::string>
    RenderGraph::prepareRecordingExpected(VriCommandBuffer*                                   cmd,
                                          std::span<const PassRecording>                      recordings,
                                          std::optional<std::span<const RecordingDependency>> order,
                                          const Profiler*                                     profiler,
                                          std::span<const HistoryResetRecording>              historyResets,
                                          std::span<const QueueFamilyHandoff>                 handoffs) const
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        if (!cmd)
        {
            return std::unexpected("Graph execution requires a command buffer");
        }
        if (!m_Compiled)
        {
            return std::unexpected("Compile the graph before Execute");
        }
        if (profiler && !profiler->belongsTo(m_Device))
        {
            return std::unexpected("Profiler belongs to another device");
        }
        for (const auto& pass : m_Passes)
        {
            if (pass.live && !pass.executeExpected)
            {
                return std::unexpected("Graph contains a legacy pass without expected error propagation: " + pass.name);
            }
        }

        auto destinations = passDestinationsExpected(cmd, recordings);
        if (!destinations)
        {
            return std::unexpected(destinations.error());
        }
        bool multipleCommands = std::ranges::any_of(recordings,
                                                    [cmd](const PassRecording& pass)
                                                    {
                                                        return pass.commands != cmd;
                                                    });
        auto clears = planHistoryClearsExpected(cmd, historyResets);
        if (!clears)
        {
            return std::unexpected(clears.error());
        }
        multipleCommands |=
            std::ranges::any_of(historyResets,
                                [cmd](const HistoryResetRecording& placement)
                                {
                                    return placement.release || placement.frame->recordingDestination() != cmd;
                                });
        if (multipleCommands && (!supportsRoutedRecording() || profiler))
        {
            return std::unexpected("Multiple recording destinations require no whole-resource reuse or profiler");
        }

        auto transfers = planHandoffsExpected(handoffs, *destinations, *clears);
        if (!transfers)
        {
            return std::unexpected(transfers.error());
        }
        RecordingOrder recordingOrder;
        if (multipleCommands || order)
        {
            auto validated =
                validateRecordingDependenciesExpected(cmd, *destinations, order, *clears, historyResets, *transfers);
            if (!validated)
            {
                return std::unexpected(validated.error());
            }
            recordingOrder = std::move(*validated);
        }
        const std::array                         initialStream {cmd};
        const std::span<VriCommandBuffer* const> streams =
            recordingOrder.streams.empty() ? std::span<VriCommandBuffer* const>(initialStream) :
                                             std::span<VriCommandBuffer* const>(recordingOrder.streams);
        auto families = planRecordingFamiliesExpected(streams, *clears, recordingOrder.passStreams, *transfers);
        if (!families)
        {
            return std::unexpected(families.error());
        }
        if (multipleCommands && !m_Device.core.CmdAliasMemoryBarrier)
        {
            return std::unexpected("Routed recording requires a native memory dependency command");
        }
        return PreparedRecording {std::move(recordingOrder),
                                  std::move(*families),
                                  std::move(*clears),
                                  std::move(*transfers),
                                  multipleCommands};
    }

    std::expected<void, std::string>
    RenderGraph::executeRecordingsExpected(VriCommandBuffer*                                   cmd,
                                           std::span<const PassRecording>                      recordings,
                                           std::optional<std::span<const RecordingDependency>> order,
                                           Profiler*                                           profiler,
                                           std::span<const HistoryResetRecording>              historyResets,
                                           std::span<const QueueFamilyHandoff>                 handoffs)
    {
        auto prepared = prepareRecordingExpected(cmd, recordings, order, profiler, historyResets, handoffs);
        if (!prepared)
        {
            return std::unexpected(prepared.error());
        }
        if (std::ranges::any_of(historyResets,
                                [](const HistoryResetRecording& placement)
                                {
                                    return placement.frame->m_State != Frame::State::eRecording;
                                }))
        {
            return std::unexpected("Begin every history clear Frame before graph recording");
        }
        const auto&     recordingOrder   = prepared->order;
        const bool      multipleCommands = prepared->routed;
        RoutedResources routed;

        struct ResourceScope
        {
            std::span<Entry> entries;
            RoutedResources& routed;

            ~ResourceScope()
            {
                for (size_t i = 0; i < routed.textures.size(); ++i)
                {
                    if (entries[i].texture &&
                        entries[i].texture->m_CallbackAccess == &routed.textures[i].callbackAccess)
                    {
                        entries[i].texture->m_CallbackAccess = nullptr;
                    }
                    if (entries[i].buffer && entries[i].buffer->m_CallbackAccess == &routed.buffers[i].access)
                    {
                        entries[i].buffer->m_CallbackAccess = nullptr;
                    }
                }
            }
        } resourceScope {m_Resources, routed};
        if (multipleCommands)
        {
            routed.textures.resize(m_Resources.size());
            routed.buffers.resize(m_Resources.size());
            for (size_t i = 0; i < m_Resources.size(); ++i)
            {
                auto& entry = m_Resources[i];
                if (entry.texture)
                {
                    if (entry.texture->m_CallbackAccess)
                    {
                        return std::unexpected(entry.name + ": texture is already tracked by a routed recording");
                    }
                    auto& access                    = routed.textures[i].callbackAccess;
                    access                          = {&routed.callback, i, {}};
                    entry.texture->m_CallbackAccess = &access;
                }
                if (entry.buffer)
                {
                    if (entry.buffer->m_CallbackAccess)
                    {
                        return std::unexpected(entry.name + ": buffer is already tracked by a routed recording");
                    }
                    auto& access                   = routed.buffers[i].access;
                    access                         = {&routed.callback, i, {}};
                    entry.buffer->m_CallbackAccess = &access;
                }
            }
        }

        const GraphStateScope transaction(m_StateTransactionActive);
        auto                  checkpoint = captureCheckpointExpected();
        if (!checkpoint)
        {
            return std::unexpected(checkpoint.error());
        }
        const auto rollback = [&](std::string error) -> std::expected<void, std::string>
        {
            if (auto restored = restoreCheckpointExpected(std::move(*checkpoint)); !restored)
            {
                return std::unexpected(error + "; state rollback failed: " + restored.error());
            }
            return std::unexpected(std::move(error));
        };
        if (profiler)
        {
            noteRecording(cmd);
            profiler->beginFrame(cmd);
        }
        if (auto reset = recordHistoryClearsExpected(prepared->historyClears, multipleCommands); !reset)
        {
            return rollback("Reset graph history: " + reset.error());
        }
        size_t recordingIndex = 0;
        size_t handoffIndex   = 0;
        for (const auto& pass : m_Passes)
        {
            if (!pass.live)
            {
                continue;
            }
            if (!recordings.empty())
            {
                cmd = recordings[recordingIndex++].commands;
            }
            const auto passIndex = size_t(&pass - m_Passes.data());
            while (handoffIndex < prepared->handoffs.size() &&
                   prepared->handoffs[handoffIndex].acquirePass == passIndex)
            {
                const auto& handoff = prepared->handoffs[handoffIndex++];
                if (auto moved = recordOwnershipTransferExpected(handoff.selection, handoff.release, cmd, handoff.next);
                    !moved)
                {
                    return rollback(pass.name + ": " + moved.error());
                }
            }
            noteRecording(cmd);
            m_Device.core.CmdBeginDebugGroup(cmd, pass.name.c_str());
            if (profiler)
            {
                if (auto result = profiler->beginPassExpected(cmd, pass.name); !result)
                {
                    m_Device.core.CmdEndDebugGroup(cmd);
                    return rollback(pass.name + ": " + result.error());
                }
            }
            if (auto activated = preparePassMemoryExpected(cmd, size_t(&pass - m_Passes.data()), multipleCommands);
                !activated)
            {
                m_Device.core.CmdEndDebugGroup(cmd);
                return rollback(pass.name + ": " + activated.error());
            }
            for (const auto& use : pass.uses)
            {
                auto&      entry = m_Resources[use.resource.index];
                const auto state = stateForUsage(use.usage);
                if (multipleCommands)
                {
                    if (auto valid = trackRoutedLayoutsExpected(use,
                                                                size_t(&pass - m_Passes.data()),
                                                                false,
                                                                recordingOrder,
                                                                routed.textures[use.resource.index]);
                        !valid)
                    {
                        m_Device.core.CmdEndDebugGroup(cmd);
                        return rollback(pass.name + ": " + valid.error());
                    }
                }
                if (entry.isTexture)
                {
                    if (auto transitioned = entry.texture->transitionExpected(cmd,
                                                                              state,
                                                                              transitionView(*entry.texture, use.range),
                                                                              multipleCommands);
                        !transitioned)
                    {
                        m_Device.core.CmdEndDebugGroup(cmd);
                        return rollback(pass.name + ": " + transitioned.error());
                    }
                }
                else
                {
                    if (auto transitioned = entry.buffer->transitionExpected(cmd,
                                                                             {state.access, state.stages},
                                                                             use.bufferRange.offset,
                                                                             use.bufferRange.size,
                                                                             multipleCommands);
                        !transitioned)
                    {
                        m_Device.core.CmdEndDebugGroup(cmd);
                        return rollback(pass.name + ": " + transitioned.error());
                    }
                }
            }
            if (profiler)
            {
                if (auto result = profiler->beginCommandsExpected(cmd); !result)
                {
                    m_Device.core.CmdEndDebugGroup(cmd);
                    return rollback(pass.name + ": " + result.error());
                }
            }
            auto result = multipleCommands ? executeRoutedPassExpected(cmd,
                                                                       size_t(&pass - m_Passes.data()),
                                                                       recordingOrder,
                                                                       routed) :
                                             pass.executeExpected(cmd, *this);
            if (result && profiler)
            {
                result = profiler->endPassExpected(cmd);
            }
            m_Device.core.CmdEndDebugGroup(cmd);
            if (!result)
            {
                return rollback(pass.name + ": " + result.error());
            }
        }
        if (profiler)
        {
            if (auto result = profiler->resolveExpected(cmd); !result)
            {
                return rollback(result.error());
            }
        }
        publishRecordingFamilies(std::move(prepared->families));
        return {};
    }

    void RenderGraph::bind(Resource resource, Texture& texture)
    {
        if (auto bound = bindExpected(resource, texture); !bound)
        {
            throw std::invalid_argument(bound.error());
        }
    }

    std::expected<void, std::string> RenderGraph::bindExpected(Resource resource, Texture& texture)
    {
        const ImportBinding binding(resource, texture);
        return bindExpected(std::span(&binding, 1));
    }

    std::expected<void, std::string> RenderGraph::bindExpected(Resource resource, Buffer& buffer)
    {
        const ImportBinding binding(resource, buffer);
        return bindExpected(std::span(&binding, 1));
    }

    std::expected<void, std::string> RenderGraph::bindExpected(std::span<const ImportBinding> bindings)
    {
        return applyImportBindingsExpected(bindings, false)
            .transform(
                [](bool)
                {
                });
    }

    std::expected<bool, std::string>
    RenderGraph::bindAndResetDependentHistoryExpected(std::span<const ImportBinding> bindings)
    {
        return applyImportBindingsExpected(bindings, true);
    }

    std::expected<bool, std::string> RenderGraph::applyImportBindingsExpected(std::span<const ImportBinding> bindings,
                                                                              bool resetDependentHistory)
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        const auto changesBinding = [this](const ImportBinding& binding)
        {
            const auto& entry = m_Resources[binding.m_Resource.index];
            return entry.texture != binding.m_Texture || entry.buffer != binding.m_Buffer;
        };
        for (size_t i = 0; i < bindings.size(); ++i)
        {
            const auto& binding = bindings[i];
            if (!ownsResource(binding.m_Resource))
            {
                return std::unexpected("Resource belongs to another graph");
            }
            for (size_t j = 0; j < i; ++j)
            {
                if (bindings[j].m_Resource.index == binding.m_Resource.index)
                {
                    return std::unexpected("Rebinding selects the same imported resource more than once");
                }
            }
            const auto& entry = m_Resources[binding.m_Resource.index];
            if (!entry.imported || entry.isTexture != bool(binding.m_Texture))
            {
                return std::unexpected("Rebinding requires an imported resource of the same kind");
            }
            if (binding.m_Texture)
            {
                const auto& texture = *binding.m_Texture;
                if (!texture.belongsTo(m_Device) || !texture.handle)
                {
                    return std::unexpected("Invalid graph texture device or handle");
                }
                if (auto valid = validateTextureExpected(texture.desc, m_Device); !valid)
                {
                    return std::unexpected(valid.error());
                }
                const auto& desc = entry.textureDesc;
                if (texture.desc.type != desc.type || texture.desc.width != desc.width ||
                    texture.desc.height != desc.height || texture.desc.depth != desc.depth ||
                    texture.desc.layerNum != desc.layerNum || texture.desc.format != desc.format ||
                    texture.desc.mipNum != desc.mipNum || texture.desc.sampleNum != desc.sampleNum ||
                    texture.desc.usage != desc.usage || texture.desc.memoryLocation != desc.memoryLocation)
                {
                    return std::unexpected(
                        "Rebinding requires the same texture shape, format, usage and memory location");
                }
            }
            else
            {
                const auto& buffer = *binding.m_Buffer;
                if (!buffer.belongsTo(m_Device) || !buffer.handle)
                {
                    return std::unexpected("Invalid graph buffer device or handle");
                }
                const auto& desc = entry.bufferDesc;
                if (!buffer.desc.size || buffer.desc.size != desc.size ||
                    buffer.desc.structureStride != desc.structureStride || buffer.desc.usage != desc.usage ||
                    buffer.desc.memoryLocation != desc.memoryLocation)
                {
                    return std::unexpected(
                        "Rebinding requires the same buffer size, stride, usage and memory location");
                }
            }
        }
        const auto replacement = [&](size_t index) -> const ImportBinding*
        {
            for (const auto& binding : bindings)
            {
                if (binding.m_Resource.index == index)
                {
                    return &binding;
                }
            }
            return nullptr;
        };
        // Compare final identities so resource permutations do not collide with intermediate assignments.
        for (const auto& binding : bindings)
        {
            for (size_t i = 0; i < m_Resources.size(); ++i)
            {
                if (i == binding.m_Resource.index)
                {
                    continue;
                }
                const auto* next    = replacement(i);
                const auto* texture = next ? next->m_Texture : m_Resources[i].texture;
                const auto* buffer  = next ? next->m_Buffer : m_Resources[i].buffer;
                if ((binding.m_Texture && texture && binding.m_Texture->handle == texture->handle) ||
                    (binding.m_Buffer && buffer && binding.m_Buffer->handle == buffer->handle))
                {
                    return std::unexpected("Rebinding aliases another final graph resource");
                }
            }
        }
        if (std::ranges::none_of(bindings, changesBinding))
        {
            return false;
        }
        if (m_BindingEpoch == UINT64_MAX)
        {
            return std::unexpected("Graph import binding epoch exhausted");
        }
        if (resetDependentHistory)
        {
            std::vector<ResourceRange> changedImports;
            changedImports.reserve(bindings.size());
            for (const auto& binding : bindings)
            {
                if (changesBinding(binding))
                {
                    changedImports.push_back({binding.m_Resource, {}, {}});
                }
            }
            // Resolve and validate resets before publishing any pointer or native name.
            // Import descriptions are identical, so the existing compiled dependencies apply to both owners.
            if (auto reset = resetDependentHistoryExpected({}, changedImports); !reset)
            {
                return std::unexpected(reset.error());
            }
        }
        for (const auto& binding : bindings)
        {
            auto& entry   = m_Resources[binding.m_Resource.index];
            entry.texture = binding.m_Texture;
            entry.buffer  = binding.m_Buffer;
            m_Device.core.SetDebugName(entry.isTexture ? static_cast<void*>(entry.texture->handle) :
                                                         static_cast<void*>(entry.buffer->handle),
                                       entry.name.c_str());
        }
        ++m_BindingEpoch;
        return true;
    }

    Texture& RenderGraph::getTexture(Resource resource)
    {
        auto& entry = lookup(resource);
        if (!entry.isTexture || !entry.texture)
        {
            throw std::logic_error("Texture is not allocated (compile/export it first)");
        }
        return *entry.texture;
    }

    std::expected<Texture*, std::string> RenderGraph::getTextureExpected(Resource resource)
    {
        if (resource.graph != this || resource.index >= m_Resources.size())
        {
            return std::unexpected("Resource belongs to another graph");
        }
        auto& entry = m_Resources[resource.index];
        if (!entry.isTexture || !entry.texture)
        {
            return std::unexpected("Texture is not allocated (compile/export it first)");
        }
        return entry.texture;
    }

    Buffer& RenderGraph::getBuffer(Resource resource)
    {
        auto& entry = lookup(resource);
        if (entry.isTexture || !entry.buffer)
        {
            throw std::logic_error("Buffer is not allocated (compile/export it first)");
        }
        return *entry.buffer;
    }

    std::expected<Buffer*, std::string> RenderGraph::getBufferExpected(Resource resource)
    {
        if (resource.graph != this || resource.index >= m_Resources.size())
        {
            return std::unexpected("Resource belongs to another graph");
        }
        auto& entry = m_Resources[resource.index];
        if (entry.isTexture || !entry.buffer)
        {
            return std::unexpected("Buffer is not allocated (compile/export it first)");
        }
        return entry.buffer;
    }

    RenderGraph::Snapshot RenderGraph::snapshot() const
    {
        auto inspected = snapshotExpected();
        if (!inspected)
        {
            throw std::runtime_error(inspected.error()); // Legacy boundary.
        }
        return std::move(*inspected);
    }

    std::expected<RenderGraph::Snapshot, std::string> RenderGraph::snapshotExpected() const
    {
        if (!m_Compiled)
        {
            return std::unexpected("Compile the graph before inspecting it");
        }
        Snapshot result;
        result.exports = m_Exports;
        result.resources.reserve(m_Resources.size());
        result.passes.reserve(m_Passes.size());
        for (size_t i = 0; i < m_Resources.size(); ++i)
        {
            const auto& entry    = m_Resources[i];
            const bool  exported = isExported(i);
            result.resources.push_back({entry.name,
                                        entry.imported,
                                        exported,
                                        exported,
                                        entry.isTexture,
                                        entry.textureDesc,
                                        entry.bufferDesc,
                                        entry.history,
                                        entry.firstUse,
                                        entry.lastUse,
                                        entry.allocation,
                                        0,
                                        false,
                                        entry.historyEpoch,
                                        entry.historyResetPending,
                                        entry.heap,
                                        entry.heapOffset,
                                        entry.heapBytes});
            for (size_t cell = 0; cell < entry.historyCells.size(); ++cell)
            {
                const auto range = textureCellRange(entry.textureDesc, cell);
                result.historyCells.push_back(
                    {uint32_t(i), range.baseMip, range.baseLayer, range.aspect, entry.historyCells[cell]});
            }
            if (entry.historyInitialBuffer)
            {
                result.bufferHistories.push_back({uint32_t(i), entry.bufferDesc.size, 0, false, entry.bufferHistory});
            }
        }
        for (const auto& pass : m_Passes)
        {
            PassInfo info {pass.name, pass.live, pass.dependencies, {}, pass.contentDependencies};
            for (const auto& use : pass.uses)
            {
                info.uses.push_back({use.resource.index, use.usage, use.discardPrevious, use.range, use.bufferRange});
                if (pass.live)
                {
                    result.resources[use.resource.index].active = true;
                }
            }
            result.passes.push_back(std::move(info));
        }
        for (const auto& heap : m_Heaps)
        {
            result.heaps.push_back(
                {heap.requirements.size, heap.requirements.alignment, heap.requirements.location, heap.resources});
        }
        uint32_t   objectCount = 0;
        const auto enumerated  = m_Device.core.EnumerateObjects ?
                                     m_Device.core.EnumerateObjects(m_Device.handle, &objectCount, nullptr) :
                                     VriResult_Unsupported;
        if (enumerated != VriResult_Unsupported)
        {
            if (enumerated != VriResult_Success)
            {
                return std::unexpected("Count graph device allocations failed: " + std::to_string(enumerated));
            }
            std::vector<VriObjectInfo> objects(objectCount);
            if (objectCount != 0)
            {
                const auto status = m_Device.core.EnumerateObjects(m_Device.handle, &objectCount, objects.data());
                if (status != VriResult_Success)
                {
                    return std::unexpected("Inspect graph device allocations failed: " + std::to_string(status));
                }
                objects.resize(objectCount);
            }
            for (size_t i = 0; i < m_Resources.size(); ++i)
            {
                const auto& entry  = m_Resources[i];
                const void* handle = nullptr;
                if (entry.isTexture && entry.texture)
                {
                    handle = entry.texture->handle;
                }
                else if (!entry.isTexture && entry.buffer)
                {
                    handle = entry.buffer->handle;
                }
                const auto object = std::ranges::find(objects, handle, &VriObjectInfo::handle);
                if (handle && object != objects.end() && entry.heap == UINT32_MAX)
                {
                    result.resources[i].memoryBytes = object->memoryBytes;
                    result.resources[i].memoryKnown = true;
                }
            }
            for (auto& history : result.bufferHistories)
            {
                const auto* handle = m_Resources[history.resourceIndex].historyInitialBuffer->handle;
                const auto  object = std::ranges::find(objects, handle, &VriObjectInfo::handle);
                if (object != objects.end())
                {
                    history.initialMemoryBytes = object->memoryBytes;
                    history.initialMemoryKnown = true;
                }
            }
        }
        return result;
    }

    Device& RenderGraph::device() const
    {
        return m_Device;
    }

    RenderGraph::ResourceInfo RenderGraph::resourceInfo(Resource resource) const
    {
        auto inspected = resourceInfoExpected(resource);
        if (!inspected)
        {
            throw std::invalid_argument(inspected.error()); // Legacy boundary.
        }
        return std::move(*inspected);
    }

    std::expected<RenderGraph::ResourceInfo, std::string> RenderGraph::resourceInfoExpected(Resource resource) const
    {
        if (!ownsResource(resource))
        {
            return std::unexpected("Resource belongs to another graph");
        }
        const auto& entry    = m_Resources[resource.index];
        const bool  exported = isExported(resource.index);
        bool        active   = m_Compiled && exported;
        if (m_Compiled && !active)
        {
            for (const auto& pass : m_Passes)
            {
                if (pass.live && std::ranges::any_of(pass.uses,
                                                     [resource](const Use& use)
                                                     {
                                                         return use.resource.index == resource.index;
                                                     }))
                {
                    active = true;
                    break;
                }
            }
        }
        return ResourceInfo {entry.name,
                             entry.imported,
                             exported,
                             active,
                             entry.isTexture,
                             entry.textureDesc,
                             entry.bufferDesc,
                             entry.history,
                             entry.firstUse,
                             entry.lastUse,
                             entry.allocation,
                             0,
                             false,
                             entry.historyEpoch,
                             entry.historyResetPending,
                             entry.heap,
                             entry.heapOffset,
                             entry.heapBytes};
    }

    std::expected<std::vector<std::string>, std::string> RenderGraph::planActivePassesExpected()
    {
        if (auto idle = checkGraphIdleExpected(); !idle)
        {
            return std::unexpected(idle.error());
        }
        if (m_RecoveryFailed)
        {
            return std::unexpected("Rebuild the graph after failed pass-state recovery");
        }
        if (!m_Compiled)
        {
            if (auto planned = planPassesExpected(); !planned)
            {
                return std::unexpected(planned.error());
            }
        }
        return activePasses();
    }

    std::vector<std::string> RenderGraph::activePasses() const
    {
        std::vector<std::string> result;
        for (const auto& pass : m_Passes)
        {
            if (pass.live)
            {
                result.push_back(pass.name);
            }
        }
        return result;
    }
} // namespace vultra
