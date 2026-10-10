#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <cstring>
#include <optional>

namespace vultra
{
    namespace
    {
        void validateTexture(const VriTextureDesc& desc)
        {
            if (desc.layerNum != 1 || desc.mipNum != 1 || desc.type != VriTextureType_2D || desc.depth != 1)
            {
                throw std::invalid_argument("Graph textures are single-mip, single-layer 2D textures");
            }
            if ((desc.usage & VriTextureUsage_DepthStencilAttachment) && desc.format != VriFormat_D32_SFLOAT)
            {
                throw std::invalid_argument("Graph depth textures currently use D32_SFLOAT");
            }
            if (desc.format == VriFormat_D32_SFLOAT &&
                (desc.usage & (VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResourceStorage)))
            {
                throw std::invalid_argument("Depth textures cannot be color or storage attachments");
            }
        }

        uint32_t requiredUsage(Usage usage, bool texture)
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
            throw std::invalid_argument("Unsupported graph usage for this resource type");
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
                    return {VriAccess_DepthStencilAttachmentRead,
                            VriLayout_DepthStencilAttachment,
                            VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests};
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
        throw std::logic_error("Unknown graph usage");
    }

    void RenderGraph::edit()
    {
        if (m_Aliased)
        {
            throw std::logic_error("Rebuild the graph to change an aliased resource plan");
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

    RenderGraph::Resource RenderGraph::importResource(std::string name, Texture& texture, bool initialized)
    {
        edit();
        validateTexture(texture.desc);
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
        m_Device.core.SetDebugName(texture.handle, entry.name.c_str());
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
        m_Device.core.SetDebugName(buffer.handle, entry.name.c_str());
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return {this, uint32_t(m_Resources.size() - 1)};
    }

    RenderGraph::Resource RenderGraph::createTexture(std::string name, const VriTextureDesc& desc)
    {
        edit();
        validateTexture(desc);
        Entry entry;
        entry.name        = std::move(name);
        entry.textureDesc = desc;
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return {this, uint32_t(m_Resources.size() - 1)};
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

    void RenderGraph::addPass(std::string name, std::initializer_list<Use> uses, ExecutePass execute, bool sideEffect)
    {
        addPass(std::move(name), std::span(uses.begin(), uses.size()), std::move(execute), sideEffect);
    }

    void RenderGraph::addPass(std::string name, std::span<const Use> uses, ExecutePass execute, bool sideEffect)
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
        m_Compiled = false;
    }

    void RenderGraph::nameResource(Resource resource, std::string name)
    {
        edit();
        if (name.empty())
        {
            throw std::invalid_argument("RenderGraph resource name is empty");
        }
        lookup(resource).name = std::move(name);
    }

    void RenderGraph::exportResource(Resource resource)
    {
        edit();
        lookup(resource);
        m_Exports.push_back(resource);
        m_Compiled = false;
    }

    RenderGraph::Resource RenderGraph::findResource(std::string_view name) const
    {
        uint32_t found = UINT32_MAX;
        for (uint32_t i = 0; i < m_Resources.size(); ++i)
        {
            if (m_Resources[i].name == name)
            {
                if (found != UINT32_MAX)
                {
                    throw std::invalid_argument("Ambiguous graph resource: " + std::string(name));
                }
                found = i;
            }
        }
        if (found == UINT32_MAX)
        {
            throw std::invalid_argument("Unknown graph resource: " + std::string(name));
        }
        return {this, found};
    }

    RenderGraph::Resource RenderGraph::captureAfterPass(const std::string& passName, Resource source, std::string name)
    {
        edit();
        const auto& entry = lookup(source);
        if (!entry.isTexture || !(entry.textureDesc.usage & VriTextureUsage_TransferSrc))
        {
            throw std::invalid_argument("Capture source must be a TransferSrc texture");
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
                throw std::invalid_argument("Captured texture has no writer: " + entry.name);
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
                throw std::invalid_argument("Capture requires one pass named: " + passName);
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
            throw std::invalid_argument(passName + " does not write the captured texture");
        }
        auto desc = entry.textureDesc;
        desc.usage |= VriTextureUsage_TransferDst;
        const auto captured = createTexture(std::move(name), desc);
        const auto label    = "Capture after " + writer;
        m_Passes.insert(m_Passes.begin() + (pass - m_Passes.begin()) + 1,
                        {label,
                         {{source, Usage::eCopySource}, {captured, Usage::eCopyDestination}},
                         [this, source, captured](auto* cmd, RenderGraph& graph)
                         {
                             VriTextureCopyDesc copy {};
                             copy.src.layerNum = 1;
                             copy.dst.layerNum = 1;
                             copy.src.aspect   = graph.getTexture(source).desc.format == VriFormat_D32_SFLOAT ?
                                                     VriImageAspect_Depth :
                                                     VriImageAspect_Color;
                             copy.dst.aspect   = copy.src.aspect;
                             m_Device.core.CmdCopyTexture(cmd,
                                                          graph.getTexture(captured).handle,
                                                          graph.getTexture(source).handle,
                                                          &copy);
                         }});
        exportResource(captured);
        return captured;
    }

    RenderGraph::Resource
    RenderGraph::createHistoryTexture(std::string name, const VriTextureDesc& desc, VriClearColor initial)
    {
        if (!(desc.usage & VriTextureUsage_TransferDst) || !(desc.usage & VriTextureUsage_ShaderResourceStorage) ||
            desc.sampleNum != 1 || !m_Device.core.GetDeviceDesc(m_Device.handle)->hasClearStorageTexture)
        {
            throw std::invalid_argument(
                "History textures require storage/transfer-destination usage and VRI clear support");
        }
        const auto resource = createTexture(std::move(name), desc);
        auto&      entry    = lookup(resource);
        entry.history       = true;
        entry.initialized   = true;
        entry.initial       = initial;
        m_ResetHistory      = true;
        return resource;
    }

    void RenderGraph::resetHistory()
    {
        if (m_HistoryEpoch == UINT64_MAX)
        {
            throw std::overflow_error("Graph history epoch exhausted");
        }
        ++m_HistoryEpoch;
        m_ResetHistory = true;
    }

    uint64_t RenderGraph::historyEpoch() const
    {
        return m_HistoryEpoch;
    }

    void RenderGraph::compile(bool aliasTransients)
    {
        if (m_Aliased)
        {
            if (!aliasTransients)
            {
                throw std::logic_error("An aliased graph must keep its allocation plan");
            }
            return;
        }
        if (aliasTransients && std::ranges::any_of(m_Resources,
                                                   [](const Entry& entry)
                                                   {
                                                       return entry.ownedTexture || entry.ownedBuffer;
                                                   }))
        {
            throw std::logic_error("Choose transient aliasing before the first graph compilation");
        }
        m_Compiled = false;
        std::vector<std::optional<size_t>> writer(m_Resources.size());
        std::vector<std::vector<size_t>>   readers(m_Resources.size());
        for (size_t p = 0; p < m_Passes.size(); ++p)
        {
            auto& pass = m_Passes[p];
            pass.dependencies.clear();
            pass.live = false;
            std::vector<bool> used(m_Resources.size());
            for (const auto& use : pass.uses)
            {
                auto&      entry = lookup(use.resource);
                const auto r     = use.resource.index;
                if (used[r])
                {
                    throw std::invalid_argument(pass.name + ": duplicate resource; use a ReadWrite usage");
                }
                used[r]             = true;
                const auto required = requiredUsage(use.usage, entry.isTexture);
                const auto declared = entry.isTexture ? entry.textureDesc.usage : entry.bufferDesc.usage;
                if ((declared & required) != required)
                {
                    throw std::invalid_argument(pass.name + ": resource lacks required usage flags: " + entry.name);
                }
                if (reads(use.usage) && !writer[r] && !entry.initialized)
                {
                    throw std::invalid_argument(pass.name + ": read before first write: " + entry.name);
                }
                if (writer[r])
                {
                    pass.dependencies.push_back(*writer[r]);
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
            if (m_Passes[p].sideEffect || std::ranges::any_of(m_Passes[p].uses,
                                                              [this](const Use& use)
                                                              {
                                                                  return lookup(use.resource).history &&
                                                                         writes(use.usage);
                                                              }))
            {
                pending.push_back(p);
            }
        }
        for (const auto resource : m_Exports)
        {
            if (writer[resource.index])
            {
                pending.push_back(*writer[resource.index]);
            }
            else if (!lookup(resource).initialized)
            {
                throw std::invalid_argument("Exported resource has no producer");
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
            pending.insert(pending.end(), pass.dependencies.begin(), pass.dependencies.end());
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
                    auto& entry    = lookup(use.resource);
                    entry.firstUse = std::min(entry.firstUse, uint32_t(p));
                    entry.lastUse  = uint32_t(p);
                }
            }
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
            if (aliasTransients && !entry.history && !exported(index))
            {
                for (const auto owner : allocations)
                {
                    const auto& candidate = m_Resources[owner];
                    if (ends[owner] >= entry.firstUse || candidate.history || exported(owner) ||
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
                    entry.ownedTexture = std::make_unique<Texture>(m_Device, entry.textureDesc, nullptr, aspect);
                    entry.texture      = entry.ownedTexture.get();
                    m_Device.core.SetDebugName(entry.texture->handle, entry.name.c_str());
                }
                else
                {
                    entry.ownedBuffer = std::make_unique<Buffer>(m_Device, entry.bufferDesc);
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
    }

    bool RenderGraph::aliasesTransients() const
    {
        return m_Aliased;
    }

    void RenderGraph::execute(VriCommandBuffer* cmd, Profiler* profiler)
    {
        if (!m_Compiled)
        {
            throw std::logic_error("Compile the graph before Execute");
        }
        if (profiler)
        {
            profiler->beginFrame(cmd);
        }
        if (m_ResetHistory)
        {
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
        for (const auto& pass : m_Passes)
        {
            if (pass.live)
            {
                m_Device.core.CmdBeginDebugGroup(cmd, pass.name.c_str());
                if (profiler)
                {
                    profiler->beginPass(cmd, pass.name);
                }
                for (const auto& use : pass.uses)
                {
                    auto&      entry = lookup(use.resource);
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
                    profiler->beginCommands(cmd);
                }
                pass.execute(cmd, *this);
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
    }

    void RenderGraph::bind(Resource resource, Texture& texture)
    {
        validateTexture(texture.desc);
        auto& entry = lookup(resource);
        if (!entry.imported || !entry.isTexture || texture.desc.width != entry.textureDesc.width ||
            texture.desc.height != entry.textureDesc.height || texture.desc.format != entry.textureDesc.format ||
            texture.desc.sampleNum != entry.textureDesc.sampleNum || texture.desc.usage != entry.textureDesc.usage)
        {
            throw std::invalid_argument("Rebinding requires an imported texture of the same format and extent");
        }
        for (const auto& other : m_Resources)
        {
            if (&other != &entry && other.texture && other.texture->handle == texture.handle)
            {
                throw std::invalid_argument("Rebinding aliases another graph texture");
            }
        }
        entry.texture = &texture;
        m_Device.core.SetDebugName(texture.handle, entry.name.c_str());
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

    Buffer& RenderGraph::getBuffer(Resource resource)
    {
        auto& entry = lookup(resource);
        if (entry.isTexture || !entry.buffer)
        {
            throw std::logic_error("Buffer is not allocated (compile/export it first)");
        }
        return *entry.buffer;
    }

    RenderGraph::Snapshot RenderGraph::snapshot() const
    {
        if (!m_Compiled)
        {
            throw std::logic_error("Compile the graph before inspecting it");
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
            PassInfo info {pass.name, pass.live, pass.dependencies, {}};
            for (const auto& use : pass.uses)
            {
                info.uses.push_back({use.resource.index, use.usage});
                if (pass.live)
                {
                    result.resources[use.resource.index].active = true;
                }
            }
            result.passes.push_back(std::move(info));
        }
        uint32_t   objectCount = 0;
        const auto enumerated  = m_Device.core.EnumerateObjects ?
                                     m_Device.core.EnumerateObjects(m_Device.handle, &objectCount, nullptr) :
                                     VriResult_Unsupported;
        if (enumerated != VriResult_Unsupported)
        {
            check(enumerated, "Count graph device allocations");
            std::vector<VriObjectInfo> objects(objectCount);
            if (objectCount != 0)
            {
                check(m_Device.core.EnumerateObjects(m_Device.handle, &objectCount, objects.data()),
                      "Inspect graph device allocations");
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

    RenderGraph::ResourceInfo RenderGraph::resourceInfo(Resource resource) const
    {
        if (resource.graph != this || resource.index >= m_Resources.size())
        {
            throw std::invalid_argument("Resource belongs to another graph");
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
        return {entry.name,
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
