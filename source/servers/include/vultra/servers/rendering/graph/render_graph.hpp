#pragma once
#include <vultra/drivers/rhi/resources.hpp>

#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vultra
{
    class Profiler;
    enum class Usage
    {
        eColorWrite,
        eColorReadWrite,
        eDepthWrite,
        eDepthRead,
        eDepthReadWrite,
        eSampled,
        eStorageRead,
        eStorageWrite,
        eStorageReadWrite,
        eCopySource,
        eCopyDestination,
        ePresent
    };

    const char* usageName(Usage usage);

    class RenderGraph
    {
    public:
        struct Resource
        {
            const RenderGraph* graph;
            uint32_t           index;
        };

        struct Use
        {
            Resource resource;
            Usage    usage;
        };

        struct ResourceInfo
        {
            std::string    name;
            bool           imported  = false;
            bool           exported  = false;
            bool           active    = false;
            bool           isTexture = true;
            VriTextureDesc textureDesc {};
            VriBufferDesc  bufferDesc {};
            bool           history     = false;
            uint32_t       firstUse    = UINT32_MAX;
            uint32_t       lastUse     = UINT32_MAX;
            uint32_t       allocation  = UINT32_MAX; // Equal values mean the same physical resource.
            uint64_t       memoryBytes = 0;          // VRI allocator measurement, including alignment/tiling.
            bool           memoryKnown = false;
        };

        struct PassUseInfo
        {
            uint32_t resourceIndex;
            Usage    usage;
        };

        struct PassInfo
        {
            std::string              name;
            bool                     active = false;
            std::vector<size_t>      dependencies;
            std::vector<PassUseInfo> uses;
        };

        struct Snapshot
        {
            std::vector<ResourceInfo> resources;
            std::vector<PassInfo>     passes;
        };

        using ExecutePass = std::function<void(VriCommandBuffer*, RenderGraph&)>;

        explicit RenderGraph(Device& device) :
            m_Device(device)
        {
        }

        RenderGraph(const RenderGraph&)            = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;
        Resource     importResource(std::string name, Texture& texture, bool initialized = true);
        Resource     importResource(std::string name, Buffer& buffer, bool initialized = true);
        Resource     createTexture(std::string name, const VriTextureDesc& desc);
        Resource     createBuffer(std::string name, const VriBufferDesc& desc);
        // History belongs to this graph. Its initial value is restored before the first use after a reset.
        Resource createHistoryTexture(std::string name, const VriTextureDesc& desc, VriClearColor initial = {});
        void     resetHistory(); // Between completed frames; keep allocations and descriptor identities.
        uint64_t historyEpoch() const;
        void addPass(std::string name, std::initializer_list<Use> uses, ExecutePass execute, bool sideEffect = false);
        void addPass(std::string name, std::span<const Use> uses, ExecutePass execute, bool sideEffect = false);
        void exportResource(Resource resource);
        void nameResource(Resource resource, std::string name); // Before compile; used by inspection and diagnostics.
        // Insert and export an opt-in copy after the pass writes source, before later writes overwrite it.
        Resource captureAfterPass(const std::string& passName, Resource source, std::string name);
        Resource findResource(std::string_view name) const; // Named outputs/intermediates, also before compile.
        // Aliasing reuses identical transient resources with disjoint live intervals. An aliased plan is immutable.
        void compile(bool aliasTransients = false);
        bool aliasesTransients() const;
        void execute(VriCommandBuffer* cmd, Profiler* profiler = nullptr);
        // Rebind a same-format/size imported backbuffer after acquisition, before execute.
        void                     bind(Resource resource, Texture& texture);
        Texture&                 getTexture(Resource resource);
        Buffer&                  getBuffer(Resource resource);
        Device&                  device() const;
        ResourceInfo             resourceInfo(Resource resource) const;
        std::vector<std::string> activePasses() const;
        Snapshot                 snapshot() const; // compiled pass/resource plan; no GPU readback

    private:
        struct Entry
        {
            std::string              name;
            bool                     isTexture   = true;
            bool                     imported    = false;
            bool                     initialized = false;
            VriTextureDesc           textureDesc {};
            VriBufferDesc            bufferDesc {};
            Texture*                 texture = nullptr;
            Buffer*                  buffer  = nullptr;
            std::unique_ptr<Texture> ownedTexture;
            std::unique_ptr<Buffer>  ownedBuffer;
            bool                     history = false;
            VriClearColor            initial {};
            uint32_t                 firstUse   = UINT32_MAX;
            uint32_t                 lastUse    = UINT32_MAX;
            uint32_t                 allocation = UINT32_MAX;
        };

        struct Pass
        {
            std::string         name;
            std::vector<Use>    uses;
            ExecutePass         execute;
            bool                sideEffect = false;
            bool                live       = false;
            std::vector<size_t> dependencies;
        };

        Entry&                lookup(Resource resource);
        void                  edit();
        Device&               m_Device;
        std::vector<Entry>    m_Resources;
        std::vector<Pass>     m_Passes;
        std::vector<Resource> m_Exports;
        bool                  m_Compiled     = false;
        bool                  m_Aliased      = false;
        bool                  m_ResetHistory = true;
        uint64_t              m_HistoryEpoch = 1;
    };
} // namespace vultra
