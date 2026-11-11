#include <vultra/gpu/profiling/profiler.hpp>
#include <vultra/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <ostream>

namespace vultra
{
    namespace
    {
        std::expected<void, Error> validateTexture(const VriTextureDesc& desc)
        {
            if (desc.layerNum != 1 || desc.mipNum != 1 || desc.type != VriTextureType_2D || desc.depth != 1)
            {
                return makeError("Graph textures are single-mip, single-layer 2D textures");
            }
            if ((desc.usage & VriTextureUsage_DepthStencilAttachment) && desc.format != VriFormat_D32_SFLOAT)
            {
                return makeError("Graph depth textures currently use D32_SFLOAT");
            }
            if (desc.format == VriFormat_D32_SFLOAT &&
                (desc.usage & (VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResourceStorage)))
            {
                return makeError("Depth textures cannot be color or storage attachments");
            }
            return {};
        }

        std::expected<uint32_t, Error> requiredUsage(Usage usage, bool texture)
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
                case Usage::eDepthReadWrite:
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
            }
            return makeError("Unsupported graph usage for this resource type");
        }

        bool reads(Usage u)
        {
            return u != Usage::eColorWrite && u != Usage::eDepthWrite && u != Usage::eStorageWrite &&
                   u != Usage::eCopyDestination;
        }

        bool writes(Usage u)
        {
            return u == Usage::eColorWrite || u == Usage::eColorReadWrite || u == Usage::eDepthWrite ||
                   u == Usage::eDepthReadWrite || u == Usage::eStorageWrite || u == Usage::eStorageReadWrite ||
                   u == Usage::eCopyDestination;
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
                case Usage::eDepthRead:
                    // VRI stores bound depth even with pipeline writes disabled. Preserve read-only dataflow
                    // for pass culling, but synchronize that attachment store at execution.
                case Usage::eDepthReadWrite:
                    return {VriAccess_DepthStencilAttachmentRead | VriAccess_DepthStencilAttachmentWrite,
                            VriLayout_DepthStencilAttachment,
                            VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests};
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
            }
            panic("Unknown graph usage");
        }

        // Usages whose layouts and stages are valid on a compute queue.
        bool computeUsage(Usage usage)
        {
            return usage == Usage::eSampled || usage == Usage::eStorageRead || usage == Usage::eStorageWrite ||
                   usage == Usage::eStorageReadWrite || usage == Usage::eCopySource || usage == Usage::eCopyDestination;
        }

        // A compute queue cannot name these states as the source of a transition.
        bool graphicsOnly(VriLayout layout, VriPipelineStageFlags stages)
        {
            return layout == VriLayout_ColorAttachment || layout == VriLayout_DepthStencilAttachment ||
                   layout == VriLayout_Present ||
                   (stages & (VriPipelineStage_ColorAttachmentOutput | VriPipelineStage_EarlyFragmentTests |
                              VriPipelineStage_LateFragmentTests | VriPipelineStage_VertexInput)) != 0;
        }

        std::string dotLabel(std::string_view text)
        {
            std::string result;
            for (const char character : text)
            {
                if (character == '"' || character == '\\')
                {
                    result += '\\';
                }
                result += character;
            }
            return result;
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
            case Usage::eDepthReadWrite:
                return "depth read/write";
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
        }
        panic("Unknown graph usage");
    }

    std::expected<void, Error> RenderGraph::edit() const
    {
        if (m_Aliased)
        {
            return makeError("Rebuild the graph to change an aliased resource plan");
        }
        return {};
    }

    std::expected<RenderGraph::Entry*, Error> RenderGraph::lookup(Resource resource)
    {
        if (resource.graph != this || resource.index >= m_Resources.size())
        {
            return makeError("Resource belongs to another graph");
        }
        return &m_Resources[resource.index];
    }

    std::expected<RenderGraph::Resource, Error>
    RenderGraph::importResource(std::string name, Texture& texture, bool initialized)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (auto valid = validateTexture(texture.desc); !valid)
        {
            return std::unexpected(valid.error());
        }
        for (const auto& entry : m_Resources)
        {
            if (entry.texture && entry.texture->handle == texture.handle)
            {
                return makeError("Import each texture only once");
            }
        }
        Entry entry;
        entry.name        = std::move(name);
        entry.imported    = true;
        entry.initialized = initialized;
        entry.texture     = &texture;
        entry.textureDesc = texture.desc;
        m_Device.core.SetDebugName(texture.handle, entry.name.c_str());
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return Resource {this, uint32_t(m_Resources.size() - 1)};
    }

    std::expected<RenderGraph::Resource, Error>
    RenderGraph::importResource(std::string name, Buffer& buffer, bool initialized)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        for (const auto& entry : m_Resources)
        {
            if (entry.buffer && entry.buffer->handle == buffer.handle)
            {
                return makeError("Import each buffer only once");
            }
        }
        Entry entry;
        entry.name        = std::move(name);
        entry.isTexture   = false;
        entry.imported    = true;
        entry.initialized = initialized;
        entry.buffer      = &buffer;
        entry.bufferDesc  = buffer.desc;
        m_Device.core.SetDebugName(buffer.handle, entry.name.c_str());
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return Resource {this, uint32_t(m_Resources.size() - 1)};
    }

    std::expected<RenderGraph::Resource, Error> RenderGraph::createTexture(std::string name, const VriTextureDesc& desc)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (auto valid = validateTexture(desc); !valid)
        {
            return std::unexpected(valid.error());
        }
        Entry entry;
        entry.name        = std::move(name);
        entry.textureDesc = desc;
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return Resource {this, uint32_t(m_Resources.size() - 1)};
    }

    std::expected<RenderGraph::Resource, Error> RenderGraph::createBuffer(std::string name, const VriBufferDesc& desc)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        Entry entry;
        entry.name       = std::move(name);
        entry.isTexture  = false;
        entry.bufferDesc = desc;
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return Resource {this, uint32_t(m_Resources.size() - 1)};
    }

    std::expected<void, Error>
    RenderGraph::addPass(std::string name, std::initializer_list<Use> uses, ExecutePass execute, bool sideEffect)
    {
        return addPass(std::move(name), std::span(uses.begin(), uses.size()), std::move(execute), sideEffect);
    }

    std::expected<void, Error>
    RenderGraph::addPass(std::string name, std::span<const Use> uses, ExecutePass execute, bool sideEffect)
    {
        return addPass(std::move(name), uses, std::move(execute), PassOptions {.sideEffect = sideEffect});
    }

    std::expected<void, Error>
    RenderGraph::addPass(std::string name, std::initializer_list<Use> uses, ExecutePass execute, PassOptions options)
    {
        return addPass(std::move(name), std::span(uses.begin(), uses.size()), std::move(execute), options);
    }

    std::expected<void, Error>
    RenderGraph::addPass(std::string name, std::span<const Use> uses, ExecutePass execute, PassOptions options)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (!execute)
        {
            return makeError("Pass needs an execute function");
        }
        for (const auto& use : uses)
        {
            if (auto entry = lookup(use.resource); !entry)
            {
                return std::unexpected(entry.error());
            }
        }
        m_Passes.push_back(
            {std::move(name), {uses.begin(), uses.end()}, std::move(execute), options.sideEffect, options.queue});
        m_Compiled = false;
        return {};
    }

    std::expected<void, Error> RenderGraph::nameResource(Resource resource, std::string name)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (name.empty())
        {
            return makeError("RenderGraph resource name is empty");
        }
        auto entry = lookup(resource);
        if (!entry)
        {
            return std::unexpected(entry.error());
        }
        (*entry)->name = std::move(name);
        return {};
    }

    std::expected<void, Error> RenderGraph::preserveContents(Resource resource)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        auto entry = lookup(resource);
        if (!entry)
        {
            return std::unexpected(entry.error());
        }
        (*entry)->preserve = true;
        return {};
    }

    std::expected<void, Error> RenderGraph::exportResource(Resource resource)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        if (auto entry = lookup(resource); !entry)
        {
            return std::unexpected(entry.error());
        }
        m_Exports.push_back(resource);
        m_Compiled = false;
        return {};
    }

    std::expected<RenderGraph::Resource, Error> RenderGraph::findResource(std::string_view name) const
    {
        uint32_t found = UINT32_MAX;
        for (uint32_t i = 0; i < m_Resources.size(); ++i)
        {
            if (m_Resources[i].name == name)
            {
                if (found != UINT32_MAX)
                {
                    return makeError("Ambiguous graph resource: {}", name);
                }
                found = i;
            }
        }
        if (found == UINT32_MAX)
        {
            return makeError("Unknown graph resource: {}", name);
        }
        return Resource {this, found};
    }

    std::expected<RenderGraph::Resource, Error>
    RenderGraph::captureAfterPass(const std::string& passName, Resource source, std::string name)
    {
        if (auto editable = edit(); !editable)
        {
            return std::unexpected(editable.error());
        }
        auto found = lookup(source);
        if (!found)
        {
            return std::unexpected(found.error());
        }
        const auto& entry = **found;
        if (!entry.isTexture || !(entry.textureDesc.usage & VriTextureUsage_TransferSrc))
        {
            return makeError("Capture source must be a TransferSrc texture");
        }
        auto pass = m_Passes.end();
        if (passName.empty())
        {
            for (auto candidate = m_Passes.begin(); candidate != m_Passes.end(); ++candidate)
            {
                if (std::any_of(candidate->uses.begin(),
                                candidate->uses.end(),
                                [source](const Use& use)
                                {
                                    return use.resource.index == source.index && writes(use.usage);
                                }))
                {
                    pass = candidate;
                }
            }
            if (pass == m_Passes.end())
            {
                return makeError("Captured texture has no writer: {}", entry.name);
            }
        }
        else
        {
            const auto matches = [&](const Pass& candidate)
            {
                return candidate.name == passName;
            };
            if (std::count_if(m_Passes.begin(), m_Passes.end(), matches) != 1)
            {
                return makeError("Capture requires one pass named: {}", passName);
            }
            pass = std::find_if(m_Passes.begin(), m_Passes.end(), matches);
        }
        const auto writer = pass->name;
        if (!std::any_of(pass->uses.begin(),
                         pass->uses.end(),
                         [source](const Use& use)
                         {
                             return use.resource.index == source.index && writes(use.usage);
                         }))
        {
            return makeError("{} does not write the captured texture", passName);
        }
        // entry is not used past this point: createTexture may reallocate m_Resources.
        auto desc = entry.textureDesc;
        desc.usage |= VriTextureUsage_TransferDst;
        const auto position = pass - m_Passes.begin();
        const auto created  = createTexture(std::move(name), desc);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        const auto captured = *created;
        const auto label    = "Capture after " + writer;
        m_Passes.insert(
            m_Passes.begin() + position + 1,
            {label,
             {{source, Usage::eCopySource}, {captured, Usage::eCopyDestination, true}},
             [this, source, captured](VriCommandBuffer* cmd, RenderGraph& graph) -> std::expected<void, Error>
             {
                 VriTextureCopyDesc copy {};
                 copy.src.layerNum = 1;
                 copy.dst.layerNum = 1;
                 copy.src.aspect = graph.getTexture(source).desc.format == VriFormat_D32_SFLOAT ? VriImageAspect_Depth :
                                                                                                  VriImageAspect_Color;
                 copy.dst.aspect = copy.src.aspect;
                 m_Device.core.CmdCopyTexture(cmd,
                                              graph.getTexture(captured).handle,
                                              graph.getTexture(source).handle,
                                              &copy);
                 return {};
             }});
        if (auto exported = exportResource(captured); !exported)
        {
            return std::unexpected(exported.error());
        }
        return captured;
    }

    std::expected<RenderGraph::Resource, Error>
    RenderGraph::createHistoryTexture(std::string name, const VriTextureDesc& desc, VriClearColor initial)
    {
        if (!(desc.usage & VriTextureUsage_TransferDst) || !(desc.usage & VriTextureUsage_ShaderResourceStorage) ||
            desc.sampleNum != 1 || !m_Device.core.GetDeviceDesc(m_Device.handle)->hasClearStorageTexture)
        {
            return makeError("History textures require storage/transfer-destination usage and VRI clear support");
        }
        const auto resource = createTexture(std::move(name), desc);
        if (!resource)
        {
            return resource;
        }
        auto& entry       = m_Resources[resource->index];
        entry.history     = true;
        entry.initialized = true;
        entry.initial     = initial;
        m_ResetHistory    = true;
        return resource;
    }

    void RenderGraph::resetHistory()
    {
        if (m_HistoryEpoch == UINT64_MAX)
        {
            panic("Graph history epoch exhausted");
        }
        ++m_HistoryEpoch;
        m_ResetHistory = true;
    }

    uint64_t RenderGraph::historyEpoch() const
    {
        return m_HistoryEpoch;
    }

    std::expected<void, Error> RenderGraph::planBatches(bool asyncCompute)
    {
        m_Batches.clear();
        const bool                           async = asyncCompute && m_Device.computeQueue != nullptr;
        std::vector<size_t>                  batchOf(m_Passes.size(), SIZE_MAX);
        std::array<std::optional<size_t>, 2> current;
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            const auto& pass = m_Passes[p];
            if (!pass.live)
            {
                continue;
            }
            if (pass.queue == PassQueue::eAsyncCompute)
            {
                // Checked even when the plan stays on one queue, so a graph behaves the same on every device.
                for (const auto& use : pass.uses)
                {
                    if (!computeUsage(use.usage))
                    {
                        return makeError("{}: async compute passes cannot use a resource as {}",
                                         pass.name,
                                         usageName(use.usage));
                    }
                }
            }
            const auto          queue = async ? pass.queue : PassQueue::eGraphics;
            auto&               open  = current[size_t(queue)];
            std::vector<size_t> waits;
            bool                fresh = !open;
            for (const auto dependency : pass.dependencies)
            {
                if (!m_Passes[dependency].live)
                {
                    continue;
                }
                const auto batch = batchOf[dependency];
                if (m_Batches[batch].queue != queue)
                {
                    waits.push_back(batch);
                    // A batch can only wait for batches submitted before it.
                    fresh = fresh || batch > *open;
                }
            }
            if (fresh)
            {
                m_Batches.push_back({queue});
                open = m_Batches.size() - 1;
            }
            auto& batch = m_Batches[*open];
            batch.passes.push_back(p);
            for (const auto wait : waits)
            {
                if (std::ranges::find(batch.waits, wait) == batch.waits.end())
                {
                    batch.waits.push_back(wait);
                }
            }
            batchOf[p] = *open;
        }
        if (m_Batches.size() < 2)
        {
            return {};
        }

        // Resources used from both queues are shared between their queue families.
        std::vector<uint8_t> queues(m_Resources.size());
        for (const auto& batch : m_Batches)
        {
            for (const auto p : batch.passes)
            {
                for (const auto& use : m_Passes[p].uses)
                {
                    queues[use.resource.index] |= uint8_t(1u << uint32_t(batch.queue));
                }
            }
        }
        for (size_t r = 0; r < m_Resources.size(); ++r)
        {
            auto&      entry = m_Resources[r];
            auto&      usage = entry.isTexture ? entry.textureDesc.usage : entry.bufferDesc.usage;
            const auto flag  = entry.isTexture ? uint32_t(VriTextureUsage_ConcurrentQueues) :
                                                 uint32_t(VriBufferUsage_ConcurrentQueues);
            if (queues[r] != 3 || (usage & flag) != 0)
            {
                continue;
            }
            if (entry.texture || entry.buffer)
            {
                return makeError("{}: used by graphics and async compute passes; import it with "
                                 "ConcurrentQueues usage, or add those passes before the first compile",
                                 entry.name);
            }
            usage |= flag;
        }

        // When a compute pass follows a graphics pass on a resource, the graphics batch leaves the resource in
        // the state the compute pass needs. A resource whose first use in the frame is on the compute queue is
        // prepared the same way by the last graphics batch of the previous frame.
        const auto handOff = [this](size_t batch, uint32_t resource, Usage usage) -> std::expected<void, Error>
        {
            auto&      handoffs = m_Batches[batch].handoffs;
            const auto existing = std::ranges::find(handoffs, resource, &Handoff::resource);
            if (existing == handoffs.end())
            {
                handoffs.push_back({resource, usage});
            }
            else if (existing->usage != usage)
            {
                return makeError("{}: graphics and async compute passes interleave in different states",
                                 m_Resources[resource].name);
            }
            return {};
        };
        std::vector<std::optional<size_t>> lastBatch(m_Resources.size());
        std::vector<std::optional<size_t>> lastGraphics(m_Resources.size());
        std::vector<std::optional<Usage>>  firstCompute(m_Resources.size());
        for (size_t b = 0; b < m_Batches.size(); ++b)
        {
            for (const auto p : m_Batches[b].passes)
            {
                for (const auto& use : m_Passes[p].uses)
                {
                    const auto r = use.resource.index;
                    if (m_Batches[b].queue == PassQueue::eAsyncCompute)
                    {
                        if (!lastBatch[r])
                        {
                            firstCompute[r] = use.usage;
                        }
                        else if (m_Batches[*lastBatch[r]].queue == PassQueue::eGraphics)
                        {
                            if (auto handed = handOff(*lastBatch[r], r, use.usage); !handed)
                            {
                                return handed;
                            }
                        }
                    }
                    else
                    {
                        lastGraphics[r] = b;
                    }
                    lastBatch[r] = b;
                }
            }
        }
        for (uint32_t r = 0; r < m_Resources.size(); ++r)
        {
            if (firstCompute[r] && lastGraphics[r])
            {
                if (auto handed = handOff(*lastGraphics[r], r, *firstCompute[r]); !handed)
                {
                    return handed;
                }
            }
        }
        return {};
    }

    std::expected<void, Error> RenderGraph::compile(bool aliasTransients, bool asyncCompute)
    {
        if (m_Aliased)
        {
            if (!aliasTransients)
            {
                return makeError("An aliased graph must keep its allocation plan");
            }
            return {};
        }
        if (aliasTransients && std::ranges::any_of(m_Resources,
                                                   [](const Entry& entry)
                                                   {
                                                       return entry.ownedTexture || entry.ownedBuffer;
                                                   }))
        {
            return makeError("Choose transient aliasing before the first graph compilation");
        }
        m_Compiled = false;
        std::vector<std::optional<size_t>> writer(m_Resources.size());
        std::vector<std::vector<size_t>>   readers(m_Resources.size());
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            auto& pass = m_Passes[p];
            pass.dependencies.clear();
            pass.contentDependencies.clear();
            pass.live = false;
            std::vector<bool> used(m_Resources.size());
            for (const auto& use : pass.uses)
            {
                const auto  r     = use.resource.index;
                const auto& entry = m_Resources[r];
                if (used[r])
                {
                    return makeError("{}: duplicate resource; use a ReadWrite usage", pass.name);
                }
                used[r] = true;
                if (use.discardPrevious && (!writes(use.usage) || reads(use.usage)))
                {
                    return makeError("{}: discardPrevious requires a write-only usage: {}", pass.name, entry.name);
                }
                const auto required = requiredUsage(use.usage, entry.isTexture);
                if (!required)
                {
                    return std::unexpected(required.error());
                }
                const auto declared = entry.isTexture ? entry.textureDesc.usage : entry.bufferDesc.usage;
                if ((declared & *required) != *required)
                {
                    return makeError("{}: resource lacks required usage flags: {}", pass.name, entry.name);
                }
                if (reads(use.usage) && !writer[r] && !entry.initialized)
                {
                    return makeError("{}: read before first write: {}", pass.name, entry.name);
                }
                if (writer[r])
                {
                    pass.dependencies.push_back(*writer[r]);
                    if (reads(use.usage) || !use.discardPrevious)
                    {
                        pass.contentDependencies.push_back(*writer[r]);
                    }
                }
                if (writes(use.usage))
                {
                    pass.dependencies.insert(pass.dependencies.end(), readers[r].begin(), readers[r].end());
                    readers[r].clear();
                    writer[r] = p;
                }
                else
                {
                    readers[r].push_back(p);
                }
            }
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
            // Only the final history value is observable next frame; earlier writes need a live reader.
            if (m_Resources[r].history && writer[r])
            {
                pending.push_back(*writer[r]);
            }
        }
        for (const auto resource : m_Exports)
        {
            if (writer[resource.index])
            {
                pending.push_back(*writer[resource.index]);
            }
            else if (!m_Resources[resource.index].initialized)
            {
                return makeError("Exported resource has no producer");
            }
        }
        while (!pending.empty())
        {
            auto& pass = m_Passes[pending.back()];
            pending.pop_back();
            if (pass.live)
            {
                continue;
            }
            pass.live = true;
            pending.insert(pending.end(), pass.contentDependencies.begin(), pass.contentDependencies.end());
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
        if (auto planned = planBatches(asyncCompute); !planned)
        {
            return planned;
        }
        const auto exported = [this](size_t index)
        {
            return std::ranges::any_of(m_Exports,
                                       [index](Resource value)
                                       {
                                           return value.index == index;
                                       });
        };
        std::vector<uint32_t> order;
        for (uint32_t i = 0; i < m_Resources.size(); ++i)
        {
            // An exported, read-only history/import is live even without a pass.
            if (m_Resources[i].firstUse != UINT32_MAX || exported(i))
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
        for (const auto index : order)
        {
            auto& entry      = m_Resources[index];
            entry.allocation = index;
            if (entry.imported)
            {
                continue;
            }
            if (aliasTransients && !entry.history && !entry.preserve && !exported(index))
            {
                for (const auto owner : allocations)
                {
                    const auto& candidate = m_Resources[owner];
                    if (ends[owner] >= entry.firstUse || candidate.history || candidate.preserve || exported(owner) ||
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
                    const auto aspect =
                        entry.textureDesc.format == VriFormat_D32_SFLOAT ? VriImageAspect_Depth : VriImageAspect_Color;
                    auto texture = Texture::create(m_Device, entry.textureDesc, nullptr, aspect);
                    if (!texture)
                    {
                        return withContext(entry.name, texture.error());
                    }
                    entry.ownedTexture = std::make_unique<Texture>(std::move(*texture));
                    entry.texture      = entry.ownedTexture.get();
                    m_Device.core.SetDebugName(entry.texture->handle, entry.name.c_str());
                }
                else
                {
                    auto buffer = Buffer::create(m_Device, entry.bufferDesc);
                    if (!buffer)
                    {
                        return withContext(entry.name, buffer.error());
                    }
                    entry.ownedBuffer = std::make_unique<Buffer>(std::move(*buffer));
                    entry.buffer      = entry.ownedBuffer.get();
                    m_Device.core.SetDebugName(entry.buffer->handle, entry.name.c_str());
                }
            }
            if (entry.allocation == index)
            {
                allocations.push_back(index);
                ends[index] = entry.lastUse;
            }
        }
        m_Aliased  = aliasTransients;
        m_Compiled = true;
        return {};
    }

    bool RenderGraph::aliasesTransients() const
    {
        return m_Aliased;
    }

    void RenderGraph::resetHistoryIfRequested(VriCommandBuffer* cmd)
    {
        if (!m_ResetHistory)
        {
            return;
        }
        m_Device.core.CmdBeginDebugGroup(cmd, "Reset graph history");
        for (auto& entry : m_Resources)
        {
            if (entry.history && entry.texture)
            {
                entry.texture->transition(cmd, stateForUsage(Usage::eCopyDestination));
                m_Device.core.CmdClearStorageTexture(cmd, entry.texture->handle, &entry.initial);
            }
        }
        m_Device.core.CmdEndDebugGroup(cmd);
        m_ResetHistory = false;
    }

    std::expected<void, Error> RenderGraph::recordPass(VriCommandBuffer* cmd, const Pass& pass, Profiler* profiler)
    {
        m_Device.core.CmdBeginDebugGroup(cmd, pass.name.c_str());
        if (profiler)
        {
            if (auto begun = profiler->beginPass(cmd, pass.name); !begun)
            {
                return begun;
            }
        }
        for (const auto& use : pass.uses)
        {
            auto&      entry = m_Resources[use.resource.index];
            const auto state = stateForUsage(use.usage);
            if (entry.isTexture)
            {
                entry.texture->transition(cmd, state);
            }
            else
            {
                entry.buffer->transition(cmd, {state.access, state.stages});
            }
        }
        if (profiler)
        {
            if (auto begun = profiler->beginCommands(cmd); !begun)
            {
                return begun;
            }
        }
        if (auto recorded = pass.execute(cmd, *this); !recorded)
        {
            return withContext(pass.name, recorded.error());
        }
        if (profiler)
        {
            if (auto ended = profiler->endPass(cmd); !ended)
            {
                return ended;
            }
        }
        m_Device.core.CmdEndDebugGroup(cmd);
        return {};
    }

    std::expected<void, Error> RenderGraph::execute(VriCommandBuffer* cmd, Profiler* profiler)
    {
        if (!m_Compiled)
        {
            return makeError("Compile the graph before Execute");
        }
        if (m_Batches.size() > 1)
        {
            return makeError("This graph plan uses the compute queue; execute it with a Frame");
        }
        if (profiler)
        {
            profiler->beginFrame(cmd);
        }
        resetHistoryIfRequested(cmd);
        for (const auto& pass : m_Passes)
        {
            if (pass.live)
            {
                if (auto recorded = recordPass(cmd, pass, profiler); !recorded)
                {
                    return recorded;
                }
            }
        }
        if (profiler)
        {
            return profiler->resolve(cmd);
        }
        return {};
    }

    std::expected<void, Error> RenderGraph::execute(Frame& frame)
    {
        if (!m_Compiled)
        {
            return makeError("Compile the graph before Execute");
        }
        const auto graphics = frame.commands(FrameQueue::eGraphics);
        if (!graphics)
        {
            return std::unexpected(graphics.error());
        }
        resetHistoryIfRequested(*graphics);
        // Timeline point each submitted batch signals on its own queue.
        std::vector<uint64_t> points(m_Batches.size());
        const auto            waitPoint = [&](const Batch& batch)
        {
            uint64_t point = 0;
            for (const auto wait : batch.waits)
            {
                point = std::max(point, points[wait]);
            }
            return point;
        };
        for (size_t b = 0; b < m_Batches.size(); ++b)
        {
            const auto& batch   = m_Batches[b];
            const bool  compute = batch.queue == PassQueue::eAsyncCompute;
            const auto  opened  = frame.commands(compute ? FrameQueue::eCompute : FrameQueue::eGraphics);
            if (!opened)
            {
                return std::unexpected(opened.error());
            }
            auto* cmd = *opened;
            for (const auto p : batch.passes)
            {
                const auto& pass = m_Passes[p];
                if (compute)
                {
                    for (const auto& use : pass.uses)
                    {
                        const auto& entry = m_Resources[use.resource.index];
                        if (entry.isTexture ? graphicsOnly(entry.texture->state.layout, entry.texture->state.stages) :
                                              graphicsOnly(VriLayout_Undefined, entry.buffer->state.stages))
                        {
                            return makeError("{}: {} is in a graphics-only state; a graphics pass of this graph "
                                             "must use it before the async compute pass",
                                             pass.name,
                                             entry.name);
                        }
                    }
                }
                if (auto recorded = recordPass(cmd, pass, nullptr); !recorded)
                {
                    return recorded;
                }
            }
            for (const auto& handoff : batch.handoffs)
            {
                auto&      entry = m_Resources[handoff.resource];
                const auto state = stateForUsage(handoff.usage);
                if (entry.isTexture)
                {
                    entry.texture->transition(cmd, state);
                }
                else
                {
                    entry.buffer->transition(cmd, {state.access, state.stages});
                }
            }
            const bool awaited      = std::ranges::any_of(std::span(m_Batches).subspan(b + 1),
                                                          [b](const Batch& later)
                                                          {
                                                         return std::ranges::find(later.waits, b) != later.waits.end();
                                                          });
            const bool lastGraphics = !compute && std::ranges::none_of(std::span(m_Batches).subspan(b + 1),
                                                                       [](const Batch& later)
                                                                       {
                                                                           return later.queue == PassQueue::eGraphics;
                                                                       });
            if (compute || awaited || !lastGraphics)
            {
                const auto point =
                    frame.submit(compute ? FrameQueue::eCompute : FrameQueue::eGraphics, waitPoint(batch));
                if (!point)
                {
                    return std::unexpected(point.error());
                }
                points[b] = *point;
            }
            else
            {
                // The caller appends to this batch and submits it with the frame.
                frame.waitForCompute(waitPoint(batch));
            }
        }
        return {};
    }

    std::expected<void, Error> RenderGraph::writeDot(std::ostream& output) const
    {
        if (!m_Compiled)
        {
            return makeError("Compile the graph before exporting it");
        }
        output << "digraph RenderGraph {\n  rankdir=LR;\n  node [fontname=\"Helvetica\", fontsize=10];\n";
        for (size_t b = 0; b < m_Batches.size(); ++b)
        {
            const auto& batch = m_Batches[b];
            output << "  subgraph cluster_batch" << b << " {\n    label=\"batch " << b << ": "
                   << (batch.queue == PassQueue::eAsyncCompute ? "async compute" : "graphics")
                   << " queue\";\n    style=rounded;\n";
            for (const auto p : batch.passes)
            {
                output << "    P" << p << " [shape=box, style=filled, fillcolor="
                       << (batch.queue == PassQueue::eAsyncCompute ? "gold" : "orange") << ", label=\""
                       << dotLabel(m_Passes[p].name) << "\"];\n";
            }
            output << "  }\n";
        }
        std::vector<bool> used(m_Resources.size());
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            const auto& pass = m_Passes[p];
            if (!pass.live)
            {
                output << "  P" << p << " [shape=box, style=\"filled,dashed\", fillcolor=gray90, label=\""
                       << dotLabel(pass.name) << "\\n(culled)\"];\n";
            }
            for (const auto& use : pass.uses)
            {
                const auto r      = use.resource.index;
                used[r]           = true;
                const char* style = pass.live ? "" : " [color=gray60]";
                if (reads(use.usage))
                {
                    output << "  R" << r << " -> P" << p << style << ";\n";
                }
                if (writes(use.usage))
                {
                    output << "  P" << p << " -> R" << r << style << ";\n";
                }
            }
        }
        for (size_t r = 0; r < m_Resources.size(); ++r)
        {
            const auto& entry = m_Resources[r];
            if (!used[r])
            {
                continue;
            }
            const char* color = "skyblue";
            if (entry.firstUse == UINT32_MAX)
            {
                color = "gray90";
            }
            else if (entry.imported)
            {
                color = "lightgreen";
            }
            output << "  R" << r << " [shape=box, style=\"rounded,filled\", fillcolor=" << color << ", label=\""
                   << dotLabel(entry.name) << "\\n";
            if (entry.isTexture)
            {
                output << entry.textureDesc.width << "x" << entry.textureDesc.height;
            }
            else
            {
                output << entry.bufferDesc.size << " bytes";
            }
            output << "\"];\n";
        }
        for (size_t b = 0; b < m_Batches.size(); ++b)
        {
            for (const auto wait : m_Batches[b].waits)
            {
                output << "  P" << m_Batches[wait].passes.back() << " -> P" << m_Batches[b].passes.front()
                       << " [style=dashed, constraint=false, label=\"wait\"];\n";
            }
        }
        output << "}\n";
        return {};
    }

    std::expected<void, Error> RenderGraph::bind(Resource resource, Texture& texture)
    {
        if (auto valid = validateTexture(texture.desc); !valid)
        {
            return valid;
        }
        auto found = lookup(resource);
        if (!found)
        {
            return std::unexpected(found.error());
        }
        auto& entry = **found;
        if (!entry.imported || !entry.isTexture || texture.desc.width != entry.textureDesc.width ||
            texture.desc.height != entry.textureDesc.height || texture.desc.format != entry.textureDesc.format ||
            texture.desc.sampleNum != entry.textureDesc.sampleNum || texture.desc.usage != entry.textureDesc.usage)
        {
            return makeError("Rebinding requires an imported texture of the same format and extent");
        }
        for (const auto& other : m_Resources)
        {
            if (&other != &entry && other.texture && other.texture->handle == texture.handle)
            {
                return makeError("Rebinding aliases another graph texture");
            }
        }
        entry.texture = &texture;
        m_Device.core.SetDebugName(texture.handle, entry.name.c_str());
        return {};
    }

    Texture& RenderGraph::getTexture(Resource resource)
    {
        auto entry = lookup(resource);
        if (!entry)
        {
            panic(entry.error().message);
        }
        if (!(*entry)->isTexture || !(*entry)->texture)
        {
            panic("Texture is not allocated (compile/export it first)");
        }
        return *(*entry)->texture;
    }

    Buffer& RenderGraph::getBuffer(Resource resource)
    {
        auto entry = lookup(resource);
        if (!entry)
        {
            panic(entry.error().message);
        }
        if ((*entry)->isTexture || !(*entry)->buffer)
        {
            panic("Buffer is not allocated (compile/export it first)");
        }
        return *(*entry)->buffer;
    }

    std::expected<RenderGraph::Snapshot, Error> RenderGraph::snapshot() const
    {
        if (!m_Compiled)
        {
            return makeError("Compile the graph before inspecting it");
        }
        Snapshot result;
        result.resources.reserve(m_Resources.size());
        result.passes.reserve(m_Passes.size());
        for (size_t i = 0; i < m_Resources.size(); ++i)
        {
            const auto& entry    = m_Resources[i];
            const bool  exported = std::any_of(m_Exports.begin(),
                                               m_Exports.end(),
                                               [i](Resource resource)
                                               {
                                                  return resource.index == i;
                                               });
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
                                        entry.allocation});
        }
        for (const auto& pass : m_Passes)
        {
            PassInfo info {pass.name, pass.live, PassQueue::eGraphics, pass.dependencies, {}, pass.contentDependencies};
            for (const auto& use : pass.uses)
            {
                info.uses.push_back({use.resource.index, use.usage, use.discardPrevious});
                if (pass.live)
                {
                    result.resources[use.resource.index].active = true;
                }
            }
            result.passes.push_back(std::move(info));
        }
        for (const auto& batch : m_Batches)
        {
            result.batches.push_back({batch.queue, batch.passes, batch.waits});
            for (const auto p : batch.passes)
            {
                result.passes[p].queue = batch.queue;
            }
        }
        uint32_t   objectCount = 0;
        const auto enumerated  = m_Device.core.EnumerateObjects ?
                                     m_Device.core.EnumerateObjects(m_Device.handle, &objectCount, nullptr) :
                                     VriResult_Unsupported;
        if (enumerated != VriResult_Unsupported)
        {
            if (auto counted = check(enumerated, "Count graph device allocations"); !counted)
            {
                return std::unexpected(counted.error());
            }
            std::vector<VriObjectInfo> objects(objectCount);
            if (objectCount != 0)
            {
                if (auto inspected =
                        check(m_Device.core.EnumerateObjects(m_Device.handle, &objectCount, objects.data()),
                              "Inspect graph device allocations");
                    !inspected)
                {
                    return std::unexpected(inspected.error());
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
                if (handle && object != objects.end())
                {
                    result.resources[i].memoryBytes = object->memoryBytes;
                    result.resources[i].memoryKnown = true;
                }
            }
        }
        return result;
    }

    Device& RenderGraph::device() const
    {
        return m_Device;
    }

    std::expected<RenderGraph::ResourceInfo, Error> RenderGraph::resourceInfo(Resource resource) const
    {
        if (resource.graph != this || resource.index >= m_Resources.size())
        {
            return makeError("Resource belongs to another graph");
        }
        const auto& entry    = m_Resources[resource.index];
        const bool  exported = std::ranges::any_of(m_Exports,
                                                   [resource](Resource value)
                                                   {
                                                      return value.index == resource.index;
                                                   });
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
                             entry.allocation};
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
