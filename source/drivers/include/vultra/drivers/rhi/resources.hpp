#pragma once
#include <vultra/core/math/extent.hpp>
#include <vultra/drivers/rhi/device.hpp>

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    namespace detail
    {
        // Dense byte or texture-cell intervals. Empty spans represent a uniform resource.
        struct QueueFamilyOwnership
        {
            struct Span
            {
                uint64_t end;
                uint32_t family;
            };

            enum class Claim
            {
                eUnchanged,
                eUnowned,
                eConflict
            };

            Claim check(uint64_t begin, uint64_t end, uint32_t family) const;
            void  claim(uint64_t begin, uint64_t end, uint64_t total, uint32_t family);

            uint32_t          uniform = UINT32_MAX;
            std::vector<Span> spans;
        };

        // The first rejected wrapper operation invalidates a routed recording.
        struct ResourceCallbackRecording
        {
            VriCommandBuffer* commands       = nullptr;
            size_t            failedResource = SIZE_MAX;
            std::string_view  error;

            std::string_view reject(size_t resource, std::string_view reason);
        };
    } // namespace detail

    // Views and transitions select native mip/layer/aspect ranges.
    class Texture
    {
    public:
        class State
        {
        public:
            State() = default;
            bool belongsTo(const Texture& texture) const;
            bool operator==(const State& other) const;

        private:
            friend class Texture;
            const Texture*                    m_Owner    = nullptr;
            VriTexture*                       m_Handle   = nullptr;
            uint32_t                          m_MipNum   = 0;
            uint32_t                          m_LayerNum = 0;
            VriImageAspectFlags               m_Aspects  = VriImageAspect_None;
            VriAccessLayoutStage              m_Uniform {};
            std::vector<VriAccessLayoutStage> m_Cells;
        };

        Texture(Device&               device,
                const VriTextureDesc& desc,
                VriTexture*           borrowed = nullptr,
                VriImageAspectFlags   aspect   = VriImageAspect_None);
        static std::expected<std::unique_ptr<Texture>, std::string>
        create(Device&               device,
               const VriTextureDesc& desc,
               VriTexture*           borrowed = nullptr,
               VriImageAspectFlags   aspect   = VriImageAspect_None);
        ~Texture();
        Texture(const Texture&)            = delete;
        Texture& operator=(const Texture&) = delete;
        void     transition(VriCommandBuffer* cmd, VriAccessLayoutStage next);
        // When externally synchronized, the caller supplies the memory dependency (including any queue wait).
        // Preserve the old layout but use a queue-neutral source scope for this layout transition.
        std::expected<void, std::string> transitionExpected(VriCommandBuffer*    cmd,
                                                            VriAccessLayoutStage next,
                                                            VriTextureViewDesc   view,
                                                            bool                 externallySynchronized = false);
        // Clear complete selected color-storage subresources; zero counts select remaining mips/layers.
        // Leaves those cells in CopyDestination. On recording failure discard the commands; CPU state restores.
        // Both clear helpers use the same external synchronization contract as transitionExpected().
        std::expected<void, std::string> clearStorageExpected(VriCommandBuffer*          cmd,
                                                              const VriClearColor&       value,
                                                              VriTextureSubresourceRange range = {},
                                                              bool externallySynchronized      = false);
        // Clears complete selected depth/stencil cells, including native MSAA. None selects every format aspect.
        // Leaves selected cells in CopyDestination. Requires native support and a graphics-capable command pool.
        std::expected<void, std::string> clearDepthStencilExpected(VriCommandBuffer*           cmd,
                                                                   const VriClearDepthStencil& value,
                                                                   VriTextureSubresourceRange  range = {},
                                                                   VriImageAspectFlags aspects = VriImageAspect_None,
                                                                   bool                externallySynchronized = false);
        // Mixed states require a mip/layer/aspect query. Set only after externally recorded work or acquisition.
        std::expected<VriAccessLayoutStage, std::string> stateExpected() const;
        std::expected<VriAccessLayoutStage, std::string> stateExpected(uint32_t mip, uint32_t layer) const;
        std::expected<VriAccessLayoutStage, std::string>
             stateExpected(uint32_t mip, uint32_t layer, VriImageAspectFlags aspects) const;
        void setState(VriAccessLayoutStage state);
        // State snapshots borrow this texture's identity; retain it and restore only discarded/unsubmitted work.
        State                                      captureState() const;
        std::expected<void, std::string>           restoreUnsubmittedExpected(State state);
        VriDescriptor*                             view();
        VriDescriptor*                             mipView(uint32_t mip);
        std::expected<VriDescriptor*, std::string> viewExpected();
        // Texture may be null or this handle; Unknown format and None aspect inherit the resource defaults.
        // Different formats require MutableFormat creation and native compatibility for every declared usage.
        // MaxEnum view type inherits the dimension. Zero counts select remaining mips/layers.
        // Views are borrowed from this texture and survive until its destruction, after final GPU use.
        std::expected<VriDescriptor*, std::string> viewExpected(VriTextureViewDesc view);
        std::expected<VriDescriptor*, std::string> mipViewExpected(uint32_t mip);

        // Build a writable depth-only attachment that resolves sample zero. The caller supplies attachment
        // layouts and synchronization. An empty Load scope requires a native load-only resolve capability.
        std::expected<VriAttachmentDesc, std::string> depthResolveAttachmentExpected(Texture&            target,
                                                                                     VriAttachmentLoadOp sourceLoad,
                                                                                     bool                recordsDraws,
                                                                                     uint32_t sourceLayer = 0,
                                                                                     uint32_t targetMip   = 0,
                                                                                     uint32_t targetLayer = 0);

        // Resolves every depth/stencil aspect in the format. Bind the returned descriptor to each
        // present attachment slot, using the same views for both slots of a combined format.
        std::expected<VriAttachmentDesc, std::string>
        depthStencilResolveAttachmentExpected(Texture&            target,
                                              VriAttachmentLoadOp sourceLoad,
                                              bool                recordsDraws,
                                              uint32_t            sourceLayer = 0,
                                              uint32_t            targetMip   = 0,
                                              uint32_t            targetLayer = 0);

        // Asset uploads can preserve two-channel semantics even in an RGBA block format.
        bool isTwoChannel() const
        {
            return twoChannelEncoding || desc.format == VriFormat_RG8_UNORM || desc.format == VriFormat_BC5_UNORM;
        }

        bool           twoChannelEncoding = false;
        VriTexture*    handle             = nullptr;
        VriTextureDesc desc {};

        bool belongsTo(const Device& device) const
        {
            return &device == &m_Device;
        }

    private:
        friend class RenderGraph;

        enum CallbackCell : uint8_t
        {
            eDeclared        = 1,
            eWritable        = 2,
            ePreservingStore = 4,
            eLayoutChanged   = 8
        };

        struct CallbackAccess
        {
            detail::ResourceCallbackRecording* recording = nullptr;
            size_t                             resource  = SIZE_MAX;
            std::span<uint8_t>                 cells;
        };

        bool                                           recordCallbackAccess(VriCommandBuffer*          commands,
                                                                            VriAccessLayoutStage       next,
                                                                            VriTextureSubresourceRange range,
                                                                            VriImageAspectFlags        aspects);
        void                                           restoreTrackedState(State state);
        void commitState(VriAccessLayoutStage next, VriTextureSubresourceRange range, VriImageAspectFlags aspects);
        std::expected<VriTextureViewDesc, std::string> normalizeViewExpected(VriTextureViewDesc view) const;
        void                                           transitionRange(VriCommandBuffer*    cmd,
                                                                       VriAccessLayoutStage next,
                                                                       uint32_t             baseMip,
                                                                       uint32_t             mipNum,
                                                                       uint32_t             baseLayer,
                                                                       uint32_t             layerNum,
                                                                       VriImageAspectFlags  aspects,
                                                                       bool                 externallySynchronized = false);

        struct Uninitialized
        {};

        struct CachedView
        {
            VriTextureViewDesc description;
            VriDescriptor*     descriptor;
        };

        Texture(Device&               device,
                const VriTextureDesc& desc,
                VriTexture*           borrowed,
                VriImageAspectFlags   aspect,
                Uninitialized);
        Device&                            m_Device;
        bool                               m_Owned;
        VriImageAspectFlags                m_Aspect;
        VriAccessLayoutStage               m_State {};
        std::vector<VriAccessLayoutStage>  m_SubresourceStates;
        std::vector<VriTextureBarrierDesc> m_Barriers;
        detail::QueueFamilyOwnership       m_GraphFamilies;
        CallbackAccess*                    m_CallbackAccess = nullptr;
        // Non-owning fast paths; m_Views owns every descriptor.
        VriDescriptor*              m_View = nullptr;
        std::vector<VriDescriptor*> m_MipViews;
        std::vector<CachedView>     m_Views;
    };

    class Buffer
    {
        struct RangeState
        {
            uint64_t       offset;
            uint64_t       size;
            VriAccessStage access;
            bool           operator==(const RangeState& other) const;
        };

    public:
        class State
        {
        public:
            State() = default;
            bool belongsTo(const Buffer& buffer) const;
            bool operator==(const State& other) const;

        private:
            friend class Buffer;
            const Buffer*           m_Owner  = nullptr;
            VriBuffer*              m_Handle = nullptr;
            uint64_t                m_Size   = 0;
            VriAccessStage          m_Aggregate {};
            std::vector<RangeState> m_Ranges;
        };

        Buffer(Device& device, const VriBufferDesc& desc);
        static std::expected<std::unique_ptr<Buffer>, std::string> create(Device& device, const VriBufferDesc& desc);
        ~Buffer();
        Buffer(const Buffer&)                   = delete;
        Buffer&        operator=(const Buffer&) = delete;
        // Legacy unchecked recording. Byte-range callers should use transitionExpected().
        void transition(VriCommandBuffer* cmd, VriAccessStage next, uint64_t offset = 0, uint64_t size = 0);
        // Buffer-local bytes; zero size selects the remaining bytes. Partial transitions require native support.
        // The external dependency flag preserves CPU range states and uses a queue-neutral native source scope.
        std::expected<void, std::string> transitionExpected(VriCommandBuffer* cmd,
                                                            VriAccessStage    next,
                                                            uint64_t          offset                 = 0,
                                                            uint64_t          size                   = 0,
                                                            bool              externallySynchronized = false);
        // Mixed selections return an error; querying a smaller uniform interval remains valid.
        std::expected<VriAccessStage, std::string> stateExpected(uint64_t offset = 0, uint64_t size = 0) const;
        void                                       setState(VriAccessStage state);
        State                                      captureState() const;
        // Restore selected bytes from this buffer's snapshot, including mixed access ranges; zero size is remainder.
        // Failure restores CPU bookkeeping; discard the partially recorded commands before retrying.
        std::expected<void, std::string>
        restoreExpected(VriCommandBuffer* cmd, const State& state, uint64_t offset = 0, uint64_t size = 0);
        std::expected<void, std::string>           restoreUnsubmittedExpected(State state);
        VriBuffer*     handle = nullptr;
        VriBufferDesc  desc {};

        bool belongsTo(const Device& device) const
        {
            return &device == &m_Device;
        }

    private:
        friend class RenderGraph;
        struct CallbackRange
        {
            uint64_t offset;
            uint64_t size;
            bool writable;
        };

        struct CallbackAccess
        {
            detail::ResourceCallbackRecording* recording = nullptr;
            size_t resource = SIZE_MAX;
            std::span<const CallbackRange> ranges;
        };

        struct Uninitialized
        {};

        Buffer(Device& device, const VriBufferDesc& desc, Uninitialized);
        bool allowsCallbackAccess(VriCommandBuffer* commands, VriAccessStage access, uint64_t offset, uint64_t size);
        std::string_view rejectCallbackAccess(std::string_view error);
        void restoreTrackedState(State state);
        std::expected<uint64_t, std::string> normalizedSizeExpected(uint64_t offset, uint64_t size) const;
        void    prepareBarriers(VriAccessStage next, uint64_t offset, uint64_t size, bool externallySynchronized);
        void    commitState(VriAccessStage next, uint64_t offset, uint64_t size);
        void    splitRange(uint64_t offset);
        Device& m_Device;
        VriAccessStage                    m_State {};
        std::vector<RangeState>           m_Ranges;
        std::vector<VriBufferBarrierDesc> m_Barriers;
        detail::QueueFamilyOwnership      m_GraphFamilies;
        CallbackAccess*                  m_CallbackAccess = nullptr;
    };

    VriTextureDesc colorTexture(Extent extent, VriFormat format = VriFormat_RGBA8_UNORM);
    VriTextureDesc depthTexture(Extent extent);
    void           beginColorPass(Device&           device,
                                  VriCommandBuffer* cmd,
                                  VriDescriptor*    view,
                                  Extent            extent,
                                  const float*      clearColor = nullptr);
} // namespace vultra
