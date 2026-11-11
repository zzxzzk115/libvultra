#pragma once
#include <vultra/gpu/resources.hpp>

#include <functional>
#include <initializer_list>
#include <iosfwd>
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
        // Preserves depth values; attachment STORE still needs write synchronization.
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

    // Where a pass runs. Async compute passes may only use storage, sampled and copy usages; when the device
    // has no separate compute queue, or async compute is disabled at compile(), they run on the graphics queue
    // in the same order with the same result.
    enum class PassQueue
    {
        eGraphics,
        eAsyncCompute
    };

    struct PassOptions
    {
        // Keep the pass and its inputs live even when nothing reads its outputs.
        bool      sideEffect = false;
        PassQueue queue      = PassQueue::eGraphics;
    };

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
            // A write fully replaces the resource. The callback must not load or preserve its old content.
            bool discardPrevious = false;
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
            bool     discardPrevious = false;
        };

        struct PassInfo
        {
            std::string              name;
            bool                     active = false;
            PassQueue                queue  = PassQueue::eGraphics; // Queue the compiled plan assigned.
            std::vector<size_t>      dependencies;
            std::vector<PassUseInfo> uses;
            // Data producers needed for this pass, excluding write-after-read/write ordering alone.
            std::vector<size_t> contentDependencies;
        };

        // Consecutive passes recorded into one command buffer on one queue.
        struct BatchInfo
        {
            PassQueue           queue = PassQueue::eGraphics;
            std::vector<size_t> passes;
            std::vector<size_t> waits; // Earlier batches on the other queue that must complete first.
        };

        struct Snapshot
        {
            std::vector<ResourceInfo> resources;
            std::vector<PassInfo>     passes;
            std::vector<BatchInfo>    batches;
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
        // A cached producer may skip writes; preserve its contents without rooting or clearing it.
        void preserveContents(Resource resource);
        // History belongs to this graph. Its initial value is restored before the first use after a reset.
        Resource createHistoryTexture(std::string name, const VriTextureDesc& desc, VriClearColor initial = {});
        void     resetHistory(); // Between completed frames; keep allocations and descriptor identities.
        uint64_t historyEpoch() const;
        void addPass(std::string name, std::initializer_list<Use> uses, ExecutePass execute, bool sideEffect = false);
        void addPass(std::string name, std::span<const Use> uses, ExecutePass execute, bool sideEffect = false);
        void addPass(std::string name, std::initializer_list<Use> uses, ExecutePass execute, PassOptions options);
        void addPass(std::string name, std::span<const Use> uses, ExecutePass execute, PassOptions options);
        void exportResource(Resource resource);
        void nameResource(Resource resource, std::string name); // Before compile; used by inspection and diagnostics.
        // Insert and export an opt-in copy after the pass writes source, before later writes overwrite it.
        Resource captureAfterPass(const std::string& passName, Resource source, std::string name);
        Resource findResource(std::string_view name) const; // Named outputs/intermediates, also before compile.
        // Aliasing reuses identical transient resources with disjoint live intervals. An aliased plan is immutable.
        // The plan groups live passes into batches per queue. Async compute needs a device compute queue;
        // without one, or with asyncCompute false, every pass runs on the graphics queue in one batch.
        void compile(bool aliasTransients = false, bool asyncCompute = true);
        bool aliasesTransients() const;
        // Records a single-batch plan into the caller's graphics command buffer.
        void execute(VriCommandBuffer* cmd, Profiler* profiler = nullptr);
        // Records any plan. Every batch but the final graphics one is submitted here with the waits the plan
        // requires; the final graphics batch stays open in frame.commands(FrameQueue::eGraphics) so the caller
        // can append commands before frame.submitAndWait().
        void execute(Frame& frame);
        // Graphviz description of the compiled plan: batches per queue, culled passes and cross-queue waits.
        void writeDot(std::ostream& output) const;
        // Rebind a same-format/size imported backbuffer after acquisition, before execute.
        void                     bind(Resource resource, Texture& texture);
        Texture&                 getTexture(Resource resource);
        Buffer&                  getBuffer(Resource resource);
        Device&                  device() const;
        ResourceInfo             resourceInfo(Resource resource) const;
        std::vector<std::string> activePasses() const;
        Snapshot                 snapshot() const; // compiled pass/resource plan; no GPU readback

    private:
        friend class PassCatalog;

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
            bool                     history  = false;
            bool                     preserve = false;
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
            PassQueue           queue      = PassQueue::eGraphics;
            bool                live       = false;
            std::vector<size_t> dependencies;
            std::vector<size_t> contentDependencies;
        };

        // A resource state a graphics batch establishes after its passes, because the next user runs on the
        // compute queue and cannot transition from graphics-only layouts itself.
        struct Handoff
        {
            uint32_t resource;
            Usage    usage;
        };

        struct Batch
        {
            PassQueue            queue = PassQueue::eGraphics;
            std::vector<size_t>  passes;
            std::vector<size_t>  waits;
            std::vector<Handoff> handoffs;
        };

        Entry&                lookup(Resource resource);
        void                  edit();
        void                  planBatches(bool asyncCompute);
        void                  recordPass(VriCommandBuffer* cmd, const Pass& pass, Profiler* profiler);
        void                  resetHistoryIfRequested(VriCommandBuffer* cmd);
        std::vector<Batch>    m_Batches;
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
