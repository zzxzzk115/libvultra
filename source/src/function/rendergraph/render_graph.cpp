#include <vultra/core/profiling/profiler.hpp>
#include <vultra/function/rendergraph/render_graph.hpp>

#include <algorithm>
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
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return {this, uint32_t(m_Resources.size() - 1)};
    }

    RenderGraph::Resource RenderGraph::importResource(std::string name, Buffer& buffer, bool initialized)
    {
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
        m_Resources.push_back(std::move(entry));
        m_Compiled = false;
        return {this, uint32_t(m_Resources.size() - 1)};
    }

    RenderGraph::Resource RenderGraph::createTexture(std::string name, const VriTextureDesc& desc)
    {
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
        if (!execute)
        {
            throw std::invalid_argument("Pass needs an execute function");
        }
        for (const auto& use : uses)
        {
            lookup(use.resource);
        }
        m_Passes.push_back({std::move(name), uses, std::move(execute), sideEffect});
        m_Compiled = false;
    }

    void RenderGraph::exportResource(Resource resource)
    {
        lookup(resource);
        m_Exports.push_back(resource);
        m_Compiled = false;
    }

    void RenderGraph::compile()
    {
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
            if (m_Passes[p].sideEffect)
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
        // Declaration order is already a valid topological order: dependencies only point backward.
        // Texture/buffer allocations persist with the graph. Descriptor views are created lazily.
        for (const auto& pass : m_Passes)
        {
            if (pass.live)
            {
                for (const auto& use : pass.uses)
                {
                    auto& entry = lookup(use.resource);
                    if (!entry.imported && !entry.texture && !entry.buffer)
                    {
                        if (entry.isTexture)
                        {
                            const auto aspect = entry.textureDesc.format == VriFormat_D32_SFLOAT ?
                                                    VriImageAspect_Depth :
                                                    VriImageAspect_Color;
                            entry.ownedTexture =
                                std::make_unique<Texture>(m_Device, entry.textureDesc, nullptr, aspect);
                            entry.texture = entry.ownedTexture.get();
                        }
                        else
                        {
                            entry.ownedBuffer = std::make_unique<Buffer>(m_Device, entry.bufferDesc);
                            entry.buffer      = entry.ownedBuffer.get();
                        }
                    }
                }
            }
        }
        m_Compiled = true;
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
        for (const auto& pass : m_Passes)
        {
            if (pass.live)
            {
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
                pass.execute(cmd, *this);
                if (profiler)
                {
                    profiler->endPass(cmd);
                }
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
