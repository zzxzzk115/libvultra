#pragma once
#include <vultra/drivers/rhi/resources.hpp>

#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
        ePresent,
        // Reads draw or dispatch arguments from a buffer.
        eIndirect,
        // Reads a constant/uniform buffer in any shader stage.
        eConstantRead,
        // Writable multisample depth/stencil attachments with a native resolve. ReadWrite preserves the load.
        eDepthWriteResolve,
        eDepthReadWriteResolve,
        // Single-sample depth/stencil resolve output. The destination remains in attachment layout.
        eDepthResolveDestination,
        // Requires a preserving VRI read-only attachment and disabled depth/stencil pipeline writes.
        eDepthReadOnly
    };

    const char* usageName(Usage usage);

    class RenderGraph
    {
        using FamilyOwnership = detail::QueueFamilyOwnership;
        using FamilyPlan = std::vector<std::optional<FamilyOwnership>>;

    public:
        struct Resource
        {
            const RenderGraph* graph;
            uint32_t           index;
        };

        enum class ImportOwnership
        {
            eCallerManaged,
            eTrackFamilies
        };

        // A borrowed replacement for one imported resource. The typed constructor fixes the resource kind.
        class ImportBinding
        {
        public:
            ImportBinding(Resource resource, Texture& texture) :
                m_Resource(resource),
                m_Texture(&texture)
            {
            }

            ImportBinding(Resource resource, Buffer& buffer) :
                m_Resource(resource),
                m_Buffer(&buffer)
            {
            }

        private:
            friend class RenderGraph;
            Resource m_Resource;
            Texture* m_Texture = nullptr;
            Buffer*  m_Buffer  = nullptr;
        };

        struct TextureRange
        {
            uint32_t            baseMip   = 0;
            uint32_t            mipNum    = 0; // Zero selects remaining mips.
            uint32_t            baseLayer = 0;
            uint32_t            layerNum  = 0;                   // Zero selects remaining layers.
            VriImageAspectFlags aspect    = VriImageAspect_None; // None selects every format aspect.

            bool operator==(const TextureRange&) const = default;
        };

        struct BufferRange
        {
            uint64_t offset = 0;
            uint64_t size   = 0; // Zero selects the remaining bytes.
        };

        struct ResourceRange
        {
            Resource     resource;
            TextureRange range {};
            BufferRange  bufferRange {};
        };

        using HistorySelection = ResourceRange;

        struct HistoryInfo
        {
            uint64_t epoch        = 0;
            bool     resetPending = false;
        };

        struct HistoryCellInfo
        {
            uint32_t            resourceIndex;
            uint32_t            mip;
            uint32_t            layer;
            VriImageAspectFlags aspect;
            HistoryInfo         state;
        };

        struct BufferHistoryRange
        {
            BufferRange range;
            HistoryInfo state;
        };

        struct BufferHistoryInfo
        {
            uint32_t resourceIndex;
            uint64_t initialBytes;
            uint64_t initialMemoryBytes = 0; // Native allocator measurement; independent of logical initial bytes.
            bool     initialMemoryKnown = false;
            std::vector<BufferHistoryRange> ranges;
        };

        struct Use
        {
            Resource resource;
            Usage    usage;
            // A write fully replaces the selected cells. The callback must not preserve their old content.
            bool         discardPrevious = false;
            TextureRange range {};       // Texture cells; buffers require the default texture range.
            BufferRange  bufferRange {}; // Buffer-local bytes; textures require the default buffer range.
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
            bool           history      = false;
            uint32_t       firstUse     = UINT32_MAX;
            uint32_t       lastUse      = UINT32_MAX;
            uint32_t       allocation   = UINT32_MAX; // Equal values mean the same physical resource.
            uint64_t       memoryBytes  = 0;          // VRI allocator measurement, including alignment/tiling.
            bool           memoryKnown  = false;
            uint64_t       historyEpoch = 0; // Zero for ordinary resources; changes only when this history resets.
            bool           historyResetPending = false;
            uint32_t       heap = UINT32_MAX; // Native heap membership; allocation still identifies a resource object.
            uint64_t       heapOffset = 0;    // Byte placement and native requested footprint, not resident bytes.
            uint64_t       heapBytes  = 0;
        };

        struct HeapInfo
        {
            uint64_t              size;
            uint64_t              alignment;
            VriMemoryLocation     location;
            std::vector<uint32_t> resources;
        };

        struct PassUseInfo
        {
            uint32_t     resourceIndex;
            Usage        usage;
            bool         discardPrevious = false;
            TextureRange range {};
            BufferRange  bufferRange {};
        };

        struct PassInfo
        {
            std::string              name;
            bool                     active = false;
            std::vector<size_t>      dependencies;
            std::vector<PassUseInfo> uses;
            // Data producers needed for this pass, excluding write-after-read/write ordering alone.
            std::vector<size_t> contentDependencies;
        };

        struct ExportInfo
        {
            uint32_t     resourceIndex;
            TextureRange range;
            BufferRange  bufferRange;
        };

        struct Snapshot
        {
            std::vector<ResourceInfo> resources;
            std::vector<PassInfo>     passes;
            std::vector<HistoryCellInfo> historyCells;
            std::vector<HeapInfo>        heaps; // Requested native heap sizes, not measured resident allocation bytes.
            std::vector<BufferHistoryInfo> bufferHistories;
            std::vector<ExportInfo>        exports; // Normalized ranges; each export retains its whole allocation.
        };

        struct PassRecording
        {
            std::string_view  pass;
            VriCommandBuffer* commands = nullptr;
        };

        struct HistoryResetRecording
        {
            HistorySelection selection;
            const Frame*     frame = nullptr;
            // Optional release on the family that already owns the pending range.
            const Frame* release = nullptr;
        };

        struct RecordingDependency
        {
            VriCommandBuffer* before = nullptr;
            VriCommandBuffer* after  = nullptr;
        };

        struct QueueFamilyHandoff
        {
            ResourceRange                   selection;
            std::optional<std::string_view> releaseAfter;
            std::string_view                acquireBefore;
            // Specify either releaseAfter or the Frame that clears the selected history range.
            const Frame* releaseAfterReset = nullptr;
        };

        // Capture only reads CPU state. Restoration can restore retained resource tracking and pass state;
        // neither action issues GPU work or edits the graph. Report restoration failure through Expected.
        // Retain every captured owner. Failed restoration requires reconstructing the graph and its passes.
        using RestorePassState = std::function<std::expected<void, std::string>()>;
        using CapturePassState = std::function<std::expected<RestorePassState, std::string>()>;

        class StateCheckpoint
        {
        public:
            StateCheckpoint()                                  = default;
            StateCheckpoint(const StateCheckpoint&)            = delete;
            StateCheckpoint& operator=(const StateCheckpoint&) = delete;

            StateCheckpoint(StateCheckpoint&& other) noexcept;
            StateCheckpoint& operator=(StateCheckpoint&& other) noexcept;
            ~StateCheckpoint();

        private:
            friend class RenderGraph;

            struct State
            {
                Texture*       texture = nullptr;
                Buffer*        buffer  = nullptr;
                Texture::State textureState;
                Buffer::State  bufferState;
                FamilyOwnership familyOwnership;
                bool           historyResetPending = false;
                // Empty when all cells share the aggregate pending value; steady completed frames allocate nothing.
                std::vector<bool> historyPendingCells;
                std::vector<bool> historyPendingBufferRanges;
            };

            const RenderGraph*            graph = nullptr;
            std::vector<State>            resources;

            struct PassState
            {
                std::string      pass;
                RestorePassState restore;
            };

            struct FrameRecording
            {
                const Frame* frame;
                uint64_t     serial;
            };

            std::vector<PassState>        passStates;
            void                          releaseRecordingGuard() noexcept;

            std::vector<FrameRecording>   recordings;
            std::optional<size_t>         firstRecording;
            uint64_t                      planEpoch    = 0;
            uint64_t                      historyEpoch = 0;
            uint64_t                      bindingEpoch = 0;
        };

        using ExecutePass         = std::function<void(VriCommandBuffer*, RenderGraph&)>;
        using ExecutePassExpected = std::function<std::expected<void, std::string>(VriCommandBuffer*, RenderGraph&)>;

        explicit RenderGraph(Device& device) :
            m_Device(device)
        {
        }

        ~RenderGraph();
        RenderGraph(const RenderGraph&)            = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;
        Resource     importResource(std::string name, Texture& texture, bool initialized = true);
        Resource     importResource(std::string name, Buffer& buffer, bool initialized = true);
        // TrackFamilies follows this wrapper across graphs and rebinds. Unclaimed ranges must be unused
        // or already owned by the family of their first graph use. The caller still orders all GPU use.
        std::expected<Resource, std::string>
        importResourceExpected(std::string     name,
                               Texture&        texture,
                               bool            initialized = true,
                               ImportOwnership ownership   = ImportOwnership::eCallerManaged);
        std::expected<Resource, std::string>
                                             importResourceExpected(std::string     name,
                                                                    Buffer&         buffer,
                                                                    bool            initialized = true,
                                                                    ImportOwnership ownership = ImportOwnership::eCallerManaged);
        Resource createTexture(std::string name, const VriTextureDesc& desc);
        std::expected<Resource, std::string> createTextureExpected(std::string name, const VriTextureDesc& desc);
        Resource                             createBuffer(std::string name, const VriBufferDesc& desc);
        std::expected<Resource, std::string> createBufferExpected(std::string name, const VriBufferDesc& desc);
        // A cached producer may skip writes; preserve its contents without rooting or clearing it.
        void preserveContents(Resource resource);
        std::expected<void, std::string> preserveContentsExpected(Resource resource);
        // History belongs to this graph. Its initial value is restored before the first use after a reset.
        // Use initial.color for storage color histories and initial.depthStencil for depth/stencil histories.
        // Depth/stencil initialization requires a graphics-capable native command pool; native MSAA is supported.
        Resource createHistoryTexture(std::string name, const VriTextureDesc& desc, VriClearValue initial = {});
        std::expected<Resource, std::string>
        createHistoryTextureExpected(std::string name, const VriTextureDesc& desc, VriClearValue initial = {});
        // Own an immutable HostUpload snapshot, copied on initial use/reset. Empty initial data uses repeated zero
        // bytes. Nonempty initial data must match desc.size. TransferDst and allocated memory are required.
        std::expected<Resource, std::string> createHistoryBufferExpected(std::string                name,
                                                                         const VriBufferDesc&       desc,
                                                                         std::span<const std::byte> initial = {});
        std::expected<void, std::string> resetHistoryExpected();
        // Validate the complete selection before changing anything. An empty selection is a no-op.
        // Call between completed frames; selecting a resource twice resets it once.
        std::expected<void, std::string> resetHistoryExpected(std::span<const Resource> resources);
        // Atomic texture-cell/buffer-byte selection; the range for the other resource kind must be default.
        std::expected<void, std::string> resetHistoryExpected(std::span<const HistorySelection> selections);
        // Latest reset epoch and any pending clear in this range. Unrelated cells do not change this epoch.
        std::expected<HistoryInfo, std::string> historyInfoExpected(Resource resource, TextureRange range) const;
        std::expected<HistoryInfo, std::string> bufferHistoryInfoExpected(Resource resource, BufferRange range) const;

        // Invalidate histories reached by edited passes or changed incoming ranges of imported resources.
        // A full overwrite stops propagation of incoming data. History feedback propagates across frames.
        // Only final writes to each history cell persist as inputs for the next frame.
        // Validate the entire edit before mutation. Requires a compiled graph and completed GPU work.
        // An empty edit is a no-op. This changes history metadata and does not upload or rebind imports.
        std::expected<void, std::string>
                 resetDependentHistoryExpected(std::span<const std::string_view> changedPasses,
                                               std::span<const ResourceRange>    changedImports = {});
        void     resetHistory(); // Between completed frames; keep allocations and descriptor identities.
        uint64_t historyEpoch() const;
        void     addPass(std::string                name,
                         std::initializer_list<Use> uses,
                         ExecutePass                execute,
                         bool                       sideEffect   = false,
                         CapturePassState           captureState = {});
        void     addPass(std::string          name,
                         std::span<const Use> uses,
                         ExecutePass          execute,
                         bool                 sideEffect   = false,
                         CapturePassState     captureState = {});
        std::expected<void, std::string> addPassExpected(std::string          name,
                                                         std::span<const Use> uses,
                                                         ExecutePassExpected  execute,
                                                         bool                 sideEffect   = false,
                                                         CapturePassState     captureState = {});
        // Explicit control edge in declaration order. Retains before when after is live, without data/history
        // propagation.
        std::expected<void, std::string> addOrderingExpected(std::string_view before, std::string_view after);
        void                             exportResource(Resource resource);
        std::expected<void, std::string> exportResourceExpected(Resource resource);
        // Retain the selected initialized cells/bytes and keep the whole allocation outside transient aliasing.
        std::expected<void, std::string> exportRangeExpected(ResourceRange selection);
        void nameResource(Resource resource, std::string name); // Before compile; used by inspection and diagnostics.
        std::expected<void, std::string> nameResourceExpected(Resource resource, std::string name);
        // Insert and export an opt-in copy after the pass writes source, before later writes overwrite it.
        Resource captureAfterPass(const std::string& passName, Resource source, std::string name);
        // Copy every texture mip/layer/aspect and full 3D depth, or every buffer byte.
        // Rejection retains the compiled plan; success preserves ordering and requires recompilation.
        std::expected<Resource, std::string>
        captureAfterPassExpected(const std::string& passName, Resource source, std::string name);
        // Buffer-local selection, compacted to offset zero in a raw transfer-only device buffer.
        // Zero size selects the remainder. Empty pass name selects the last overlapping writer.
        // Textures require the default buffer range.
        std::expected<Resource, std::string>
        captureAfterPassExpected(const std::string& passName, Resource source, std::string name, BufferRange range);
        // A texture selection rebases its first mip/layer to zero. Each 3D mip retains its full depth.
        // Partial cubes use 2D views unless the selection contains complete aligned cubes.
        // The copy retains the source format and exports only the selected aspects. Live overlapping
        // accesses order around its CopySource layout; disjoint mip/layer/aspect ranges stay independent.
        std::expected<Resource, std::string>
        captureRangeAfterPassExpected(const std::string& passName, ResourceRange source, std::string name);
        // Named outputs/intermediates can be resolved before compile; duplicate names are ambiguous.
        std::expected<Resource, std::string> findResourceExpected(std::string_view name) const;
        Resource                             findResource(std::string_view name) const;
        // Aliasing reuses identical GPU-only transients with disjoint live intervals. The plan is immutable.
        void                             compile(bool aliasTransients = false);
        std::expected<void, std::string> compileExpected(bool aliasTransients = false);
        // Distinct native resources pack into disjoint regions; overlapping regions require every old use
        // to precede every new use in the DAG. Native alignment, memory types and cross-kind granularity apply.
        // First cell writes must explicitly replace contents. Imports, exports and histories remain independent.
        // Host-visible resources pack into disjoint regions; GPU ancestry does not order CPU mapping accesses.
        // Select before the first compilation; retain the graph through final use on every submitted queue.
        std::expected<void, std::string> compileHeapsExpected();
        // Use proposed completion edges as additional alias evidence. Pass recordings follow live graph order.
        // Each execution must preserve the proven lifetimes. Compilation records no commands.
        std::expected<void, std::string> compileHeapsExpected(VriCommandBuffer*                    initial,
                                                              std::span<const PassRecording>       recordings,
                                                              std::span<const RecordingDependency> order);
        bool                             aliasesTransients() const;
        // Only the legacy whole-object reuse plan depends on one linear command stream.
        bool                             supportsRoutedRecording() const;
        void                             execute(VriCommandBuffer* cmd,
                                                 Profiler*         profiler = nullptr); // Legacy error boundary; accepts both callback kinds.
        // On failure the command buffer must be discarded. Passes must declare every resource whose
        // tracked state they mutate; graph-owned/imported and registered pass CPU states restore on error.
        std::expected<void, std::string> executeExpected(VriCommandBuffer* cmd, Profiler* profiler = nullptr);
        // Explicit recording destinations, in activePasses() order. The caller supplies queue waits/signals
        // for every dependency and keeps all resources alive through the final consumer. Record every buffer
        // before submitting any of them; on failure discard them all. Histories reset on initialCommands.
        // Multiple destinations reject identical whole-resource reuse and the single-command-buffer profiler.
        // A native heap handoff across destinations requires executeOrderedExpected().
        std::expected<void, std::string> executeExpected(VriCommandBuffer*              initialCommands,
                                                         std::span<const PassRecording> recordings,
                                                         Profiler*                      profiler = nullptr);
        // Validate destinations, declared dependencies and queue families without recording or publishing state.
        // Callback access and texture layouts are checked during execution.
        std::expected<void, std::string>
        validateOrderedRecordingExpected(VriCommandBuffer*                      initial,
                                         std::span<const PassRecording>         recordings,
                                         std::span<const RecordingDependency>   order,
                                         std::span<const HistoryResetRecording> historyResets = {},
                                         std::span<const QueueFamilyHandoff>    handoffs      = {}) const;
        // Validate the caller's GPU completion order before recording. Each edge promises that
        // the earlier command buffer completes before the later one begins GPU work.
        // Every cross-buffer pass dependency and overlapping pending history clear must be reachable.
        // Explicit reset selections place pending history on the specified Frames. Other clears use initialCommands.
        // Selections must be disjoint. A Frame can serve only clears, with its own completion edges.
        // A placement can release the pending range on its previous Vulkan family before the clear.
        // Explicit Vulkan handoffs can also move a range from a pass or clear to a destination pass.
        // Order preflight leaves the graph and command buffers untouched on failure. During recording, declare
        // every texture and buffer range a callback uses or transitions, including temporary changes. Discard all
        // recordings on failure. The caller supplies waits/signals and retains resources through completion.
        std::expected<void, std::string>
        executeOrderedExpected(VriCommandBuffer*                      initialCommands,
                               std::span<const PassRecording>         recordings,
                               std::span<const RecordingDependency>   order,
                               Profiler*                              profiler      = nullptr,
                               std::span<const HistoryResetRecording> historyResets = {},
                               std::span<const QueueFamilyHandoff>    handoffs      = {});
        // Between graph recordings, transfer a tracked import or a retained graph export from its current
        // Vulkan family. Every selected range must already be owned by the releasing family.
        // A graph-owned selection must be covered by the union of its exports.
        // Record after the producer and before the consumer. The caller must submit both halves in order
        // with an all-commands semaphore dependency and retain the resource through completion.
        // Capture a checkpoint before recording and restore it only after both recordings are discarded.
        std::expected<void, std::string> transferBufferOwnershipExpected(Resource          resource,
                                                                         BufferRange       range,
                                                                         VriCommandBuffer* release,
                                                                         VriCommandBuffer* acquire,
                                                                         VriAccessStage    next);
        std::expected<void, std::string> transferTextureOwnershipExpected(Resource             resource,
                                                                          TextureRange         range,
                                                                          VriCommandBuffer*    release,
                                                                          VriCommandBuffer*    acquire,
                                                                          VriAccessLayoutStage next);
        // Recording, checkpoint capture and restoration block graph edits and reentry on the same graph.
        // Read-only graph queries and recording through the declared resources remain available to callbacks.
        // Capture before recording and restore only if every command recorded since the checkpoint
        // is known not to have been submitted. Retain the graph, imported resources and pass state owners,
        // preserving their identities. Dropping a checkpoint never rolls back accepted or completed GPU work.
        std::expected<StateCheckpoint, std::string> checkpointExpected() const;
        // Pass every Frame involved, after begin and before recording graph commands. Retain the Frames through
        // restoration. Restoration verifies the graph's actual recording destinations and requires a
        // successful checked discard of each exact recording.
        std::expected<StateCheckpoint, std::string> checkpointExpected(std::span<const Frame* const> frames) const;
        // Preflight before undoing state on other owners in a multi-graph recording.
        std::expected<void, std::string> validateCheckpointRestoreExpected(const StateCheckpoint& checkpoint) const;
        std::expected<void, std::string>            restoreUnsubmittedExpected(StateCheckpoint&& checkpoint);
        // Rebind compatible imports after prior uses finish, before recording. Borrowed views need refreshing.
        void                                 bind(Resource resource, Texture& texture);
        std::expected<void, std::string>     bindExpected(Resource resource, Texture& texture);
        std::expected<void, std::string>     bindExpected(Resource resource, Buffer& buffer);
        // Validate the complete final binding set before changing anything; permits swaps and mixed-kind batches.
        // Successful changes invalidate older checkpoints, including A->B->A cycles. No-op batches preserve them.
        std::expected<void, std::string>     bindExpected(std::span<const ImportBinding> bindings);
        // Treat changed imports as incoming content edits and reset only dependent histories.
        // A changed batch requires a compiled graph. Rejection preserves bindings and history epochs.
        // Returns true for an actual replacement and false for an empty or identity batch.
        std::expected<bool, std::string> bindAndResetDependentHistoryExpected(std::span<const ImportBinding> bindings);
        Texture&                             getTexture(Resource resource);
        std::expected<Texture*, std::string> getTextureExpected(Resource resource);
        Buffer&                              getBuffer(Resource resource);
        std::expected<Buffer*, std::string>  getBufferExpected(Resource resource);
        Device&                              device() const;

        bool ownsResource(Resource resource) const
        {
            return resource.graph == this && resource.index < m_Resources.size();
        }

        ResourceInfo             resourceInfo(Resource resource) const;
        std::expected<ResourceInfo, std::string> resourceInfoExpected(Resource resource) const;
        // Resolve live pass names for a proposed recording plan without allocating resources or recording commands.
        std::expected<std::vector<std::string>, std::string> planActivePassesExpected();
        std::vector<std::string> activePasses() const;
        Snapshot                 snapshot() const; // compiled pass/resource plan; no GPU readback
        // Native telemetry failure rejects inspection without changing the graph; unsupported telemetry is unknown.
        std::expected<Snapshot, std::string> snapshotExpected() const;

    private:
        friend class PassCatalog;
        friend class ToneMappingPass;
        friend class BuiltinRenderer;
        friend struct GraphDefinition;

        struct Entry
        {
            std::string              name;
            bool                     isTexture   = true;
            bool                     imported    = false;
            bool                     initialized = false;
            VriTextureDesc           textureDesc {};
            VriBufferDesc            bufferDesc {};
            std::vector<uint64_t>    bufferBoundaries; // Compiled byte endpoints, including zero and buffer size.
            Texture*                 texture = nullptr;
            Buffer*                  buffer  = nullptr;
            std::unique_ptr<Texture> ownedTexture;
            std::unique_ptr<Buffer>  ownedBuffer;
            std::unique_ptr<Buffer>  historyInitialBuffer; // Immutable, borrowed by reset copies through completion.
            bool                     history             = false;
            bool                     preserve            = false;
            uint64_t                 historyEpoch        = 0;
            bool                     historyResetPending = false;
            VriClearValue            initial {};
            ImportOwnership          importOwnership = ImportOwnership::eCallerManaged;
            std::vector<HistoryInfo> historyCells;
            std::vector<BufferHistoryRange> bufferHistory;
            uint32_t                 firstUse   = UINT32_MAX;
            uint32_t                 lastUse    = UINT32_MAX;
            uint32_t                 allocation = UINT32_MAX;
            uint32_t                 heap       = UINT32_MAX;
            uint64_t                 heapOffset = 0;
            uint64_t                 heapBytes  = 0;
        };

        struct Pass
        {
            std::string         name;
            std::vector<Use>    uses;
            ExecutePass         execute;
            bool                sideEffect = false;
            bool                live       = false;
            std::vector<size_t> dependencies;
            std::vector<size_t> contentDependencies;
            // Histories whose previous-frame contents are consumed before a writer in this frame.
            std::vector<std::pair<uint32_t, size_t>> historyDependencies;
            std::vector<std::pair<uint32_t, size_t>> finalHistoryWrites;
            ExecutePassExpected   executeExpected;
            CapturePassState      captureState;
            std::vector<uint32_t>                    heapActivations;
            std::vector<uint64_t>                    ancestors;
            std::vector<size_t>                      orderingDependencies;
            bool                                     capturesTexture = false;
        };

        struct Heap
        {
            std::unique_ptr<VriMemory, decltype(VriCoreInterface::FreeMemory)> memory;
            VriMemoryDesc                                                      requirements;
            std::vector<uint32_t>                                              resources;
        };

        // Each construction scope may modify only the resources and passes it appends.
        // Rollback removes command callbacks before their instance owners and appended resources expire.
        class AppendTransaction
        {
        public:
            explicit AppendTransaction(RenderGraph& graph);
            ~AppendTransaction();
            AppendTransaction(const AppendTransaction&)            = delete;
            AppendTransaction& operator=(const AppendTransaction&) = delete;

            void commit()
            {
                m_Committed = true;
            }

        private:
            friend class RenderGraph;

            RenderGraph&       m_Graph;
            AppendTransaction* m_Parent;
            size_t             m_Resources;
            size_t             m_Passes;
            size_t             m_Exports;
            bool               m_Compiled;
            bool               m_Committed = false;
        };

        struct RecordingOrder
        {
            std::vector<VriCommandBuffer*> streams;
            std::vector<size_t>            passStreams;
            std::vector<std::vector<bool>> reaches;

            bool orders(size_t before, size_t after) const
            {
                return passStreams[before] == passStreams[after] || reaches[passStreams[before]][passStreams[after]];
            }
        };

        struct HistoryClear
        {
            HistorySelection  selection;
            VriCommandBuffer* commands;
            VriCommandBuffer* release = nullptr;
        };

        struct PlannedHandoff
        {
            ResourceRange        selection;
            VriCommandBuffer*    release;
            size_t               releasePass;
            size_t               acquirePass;
            VriAccessLayoutStage next;
        };

        struct PreparedRecording
        {
            RecordingOrder              order;
            FamilyPlan                  families;
            std::vector<HistoryClear>   historyClears;
            std::vector<PlannedHandoff> handoffs;
            bool                        routed;
        };

        struct RoutedTexture
        {
            struct Cell
            {
                size_t layoutOwner = SIZE_MAX;
                size_t lastWriter  = SIZE_MAX;
            };

            struct Access
            {
                size_t       pass;
                TextureRange range;
            };

            std::vector<Cell>    cells;
            std::vector<Access>  accesses;
            std::vector<uint8_t>    callbackCells;
            Texture::CallbackAccess callbackAccess;
        };

        struct RoutedBuffer
        {
            std::vector<Buffer::CallbackRange> ranges;
            Buffer::CallbackAccess access;
        };

        struct RoutedResources
        {
            detail::ResourceCallbackRecording callback;
            std::vector<RoutedTexture> textures;
            std::vector<RoutedBuffer> buffers;
            std::vector<uint32_t> captured;
        };

        std::expected<void, std::string> planPassesExpected();
        static FamilyOwnership*          trackedFamilies(const Entry& entry);

        struct OwnershipTransfer
        {
            Entry&           entry;
            FamilyOwnership& ownership;
            uint32_t         source;
            uint32_t         destination;
        };

        std::expected<OwnershipTransfer, std::string> prepareOwnershipTransferExpected(Resource          resource,
                                                                                       VriCommandBuffer* release,
                                                                                       VriCommandBuffer* acquire,
                                                                                       bool              texture);
        std::expected<ResourceRange, std::string>     normalizeResourceRangeExpected(ResourceRange selection) const;
        std::expected<std::vector<PlannedHandoff>, std::string>
                                         planHandoffsExpected(std::span<const QueueFamilyHandoff> handoffs,
                                                              std::span<VriCommandBuffer* const>  destinations,
                                                              std::span<const HistoryClear>       clears) const;
        std::expected<void, std::string> recordOwnershipTransferExpected(const ResourceRange& selection,
                                                                         VriCommandBuffer*    release,
                                                                         VriCommandBuffer*    acquire,
                                                                         VriAccessLayoutStage next);
        std::expected<FamilyPlan, std::string>
             planRecordingFamiliesExpected(std::span<VriCommandBuffer* const> streams,
                                           std::span<const HistoryClear>      clears,
                                           std::span<const size_t>            passStreams = {},
                                           std::span<const PlannedHandoff>    handoffs    = {}) const;
        void publishRecordingFamilies(FamilyPlan plan);
        std::expected<std::vector<VriCommandBuffer*>, std::string>
        passDestinationsExpected(VriCommandBuffer* initial, std::span<const PassRecording> recordings) const;
        std::expected<RecordingOrder, std::string>
        validateRecordingDependenciesExpected(VriCommandBuffer*                                   initial,
                                              std::span<VriCommandBuffer* const>                  destinations,
                                              std::optional<std::span<const RecordingDependency>> order,
                                              std::span<const HistoryClear>                       clears,
                                              std::span<const HistoryResetRecording>              placements,
                                              std::span<const PlannedHandoff>                     handoffs) const;
        std::expected<PreparedRecording, std::string>
        prepareRecordingExpected(VriCommandBuffer*                                   initial,
                                 std::span<const PassRecording>                      recordings,
                                 std::optional<std::span<const RecordingDependency>> order,
                                 const Profiler*                                     profiler,
                                 std::span<const HistoryResetRecording>              historyResets,
                                 std::span<const QueueFamilyHandoff>                 handoffs) const;
        std::expected<void, std::string>
                                         executeRecordingsExpected(VriCommandBuffer*                                   initial,
                                                                   std::span<const PassRecording>                      recordings,
                                                                   std::optional<std::span<const RecordingDependency>> order,
                                                                   Profiler*                                           profiler,
                                                                   std::span<const HistoryResetRecording>              historyResets = {},
                                                                   std::span<const QueueFamilyHandoff>                 handoffs = {});
        std::expected<void, std::string> trackRoutedLayoutsExpected(const Use&            use,
                                                                    size_t                pass,
                                                                    bool                  afterCallback,
                                                                    const RecordingOrder& order,
                                                                    RoutedTexture&        texture) const;
        std::expected<void, std::string> executeRoutedPassExpected(VriCommandBuffer* commands,
                                                                 size_t pass,
                                                                 const RecordingOrder& order,
                                                                 RoutedResources& resources);
        std::expected<std::vector<HistoryClear>, std::string>
        planHistoryClearsExpected(VriCommandBuffer* initial, std::span<const HistoryResetRecording> placements) const;
        std::expected<void, std::string> recordHistoryClearsExpected(std::span<const HistoryClear> clears, bool routed);
        std::expected<void, std::string>
        preparePassMemoryExpected(VriCommandBuffer* commands, size_t pass, bool routed = false);
        bool isExported(size_t index) const;
        bool coversExportedRange(ResourceRange normalized) const;
        Entry&                           lookup(Resource resource);
        std::expected<void, std::string>            checkStateTransactionIdleExpected() const;
        std::expected<void, std::string>            checkGraphEditExpected() const;
        std::expected<void, std::string>            checkGraphIdleExpected() const;
        std::expected<bool, std::string>            applyImportBindingsExpected(std::span<const ImportBinding> bindings,
                                                                                bool resetDependentHistory);
        void                                        labelImport(const Entry& entry) const;
        void                                        noteRecording(VriCommandBuffer* commands);
        std::expected<StateCheckpoint, std::string> captureCheckpointExpected() const;
        std::expected<void, std::string>            restoreCheckpointExpected(StateCheckpoint&& checkpoint);
        void                             edit();
        Device&                          m_Device;
        // Declared first: resource objects and their views expire before their borrowed heap memory.
        std::vector<Heap>                m_Heaps;
        std::vector<Entry>               m_Resources;
        std::vector<Pass>                m_Passes;
        std::vector<ExportInfo>          m_Exports;

        struct HeapAlias
        {
            uint32_t before;
            uint32_t after;
            bool     requiresSubmissionProof;
        };

        // Overlapping occupants in lifetime order, including shared buffer/image granularity pages.
        std::vector<HeapAlias> m_HeapAliasPairs;
        // Live uses of occupants whose lifetimes rely on a proposed submission order.
        std::vector<std::vector<size_t>> m_HeapSubmissionUses;
        AppendTransaction*               m_ActiveAppend           = nullptr;
        mutable bool                     m_StateTransactionActive = false;
        bool                             m_RecoveryFailed = false;
        bool                             m_Compiled     = false;
        bool                             m_Aliased      = false;
        bool                             m_HeapAliasing = false;
        uint64_t                         m_HistoryEpoch = 1;
        uint64_t                         m_BindingEpoch = 0;
        uint64_t                         m_PlanEpoch              = 0;
        // Recording evidence is needed only while a guarded public checkpoint is alive.
        mutable std::vector<StateCheckpoint*>  m_RecordingGuards;
        mutable std::vector<VriCommandBuffer*> m_RecordedDestinations;
    };
} // namespace vultra
