#pragma once
#include <vri/ext/vri_ext_pipeline_cache.h>
#include <vri/vri.h>

#include <expected>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vultra
{
    class Device;
    class Frame;

    // Owns one accepted submission's completion fence, independently of its Frame. Device must outlive it.
    // CPU observation only: it cannot be borrowed as a new GPU wait whose lifetime would escape this owner.
    class SubmissionCompletion
    {
    public:
        SubmissionCompletion() = default;
        ~SubmissionCompletion();
        SubmissionCompletion(SubmissionCompletion&& other) noexcept;
        SubmissionCompletion& operator=(SubmissionCompletion&& other) noexcept;
        SubmissionCompletion(const SubmissionCompletion&)            = delete;
        SubmissionCompletion& operator=(const SubmissionCompletion&) = delete;
        // A timeout is false; native observation failure is an error and never implies completion.
        std::expected<bool, std::string> readyExpected() const;

    private:
        friend class Device;
        friend class Frame;
        SubmissionCompletion(Device& device, VriFence* fence);
        void         release() noexcept;
        Device*      m_Device   = nullptr;
        VriFence*    m_Fence    = nullptr;
        bool         m_Accepted = false;
        mutable bool m_Complete = false;
    };

    inline void check(VriResult result, const char* operation)
    {
        if (result != VriResult_Success)
        {
            throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
        }
    }

    // VRI remains the public rendering API. Owns its device/tables and explicitly deferred resource owners.
    class Device
    {
    public:
        explicit Device(const VriDeviceCreationDesc& desc);
        // Convenience construction preserves the builtin shaders' Y-up authoring convention.
        // Strict feature requirements by default; bestEffort enables explicit capability-based selection.
        explicit Device(bool        validation       = true,
                        const void* nativeCreateInfo = nullptr,
                        uint64_t    features         = 0,
                        bool        bestEffort       = false);
        explicit Device(VriGraphicsAPI api,
                        bool           validation       = true,
                        const void*    nativeCreateInfo = nullptr,
                        uint64_t       features         = 0,
                        bool           bestEffort       = false);
        static std::expected<std::unique_ptr<Device>, std::string> create(bool        validation          = true,
                                                                          const void* nativeCreateInfo    = nullptr,
                                                                          uint64_t    features            = 0,
                                                                          bool        bestEffort          = false,
                                                                          bool allowSeparateComputeFamily = false);
        static std::expected<std::unique_ptr<Device>, std::string> create(const VriDeviceCreationDesc& desc);
        ~Device();
        Device(const Device&)                                     = delete;
        Device&                          operator=(const Device&) = delete;
        void                             waitIdle() const;
        std::expected<void, std::string> waitIdleExpected() const;
        // Destructors report completion errors without throwing during stack unwinding.
        void waitIdleForDestruction() const noexcept;

        // Move ownership only on success. Supply every accepted submission that may still use the object/bundle.
        // Bundle borrowed graph dependencies and pass owners, or retain them separately through the same completions.
        template<class T, class Deleter>
        std::expected<void, std::string> deferReleaseExpected(std::unique_ptr<T, Deleter>&       resource,
                                                              std::vector<SubmissionCompletion>& lastUses)
        {
            if (!resource)
            {
                return std::unexpected("Deferred release requires a nonempty resource owner");
            }
            if (auto valid = validateCompletionsExpected(lastUses); !valid)
            {
                return valid;
            }
            m_DeferredReleases.reserve(m_DeferredReleases.size() + 1);
            using Owner = std::unique_ptr<T, Deleter>;
            auto* held  = new (std::nothrow) Owner(std::move(resource));
            if (!held)
            {
                return std::unexpected("Allocate deferred resource ownership failed");
            }
            DeferredOwner owner(held,
                                [](void* value) noexcept
                                {
                                    delete static_cast<Owner*>(value);
                                });
            m_DeferredReleases.push_back({std::move(owner), std::move(lastUses)});
            lastUses.clear();
            return {};
        }

        // Nonblocking, releases only records whose complete submission set is observed. Errors retain pending work.
        std::expected<size_t, std::string> collectDeferredExpected();

        // Requests native Vulkan family selection. The device may still use one family.
        bool allowsSeparateComputeFamily() const
        {
            return m_AllowSeparateComputeFamily;
        }

        VriDevice*            handle   = nullptr;
        uint64_t              features = 0;
        VriQueue*             queue    = nullptr;
        VriCoreInterface      core {};
        VriSwapChainInterface swap {};
        // Pass this device-owned cache to graphics/compute VRI descriptors.
        VriPipelineCache* pipelineCache = nullptr;

    private:
        using DeferredOwner = std::unique_ptr<void, void (*)(void*)>;

        struct DeferredRelease
        {
            DeferredOwner resource;
            // Destruction waits for completions before releasing the captured owner.
            std::vector<SubmissionCompletion> lastUses;
        };
        struct Uninitialized
        {};

        explicit Device(Uninitialized)
        {
        }

        Device(const VriDeviceCreationDesc& desc, bool automaticSelection);
        std::expected<void, std::string> initialize(const VriDeviceCreationDesc& desc, bool automaticSelection);
        std::expected<void, std::string>
        validateCompletionsExpected(std::span<const SubmissionCompletion> lastUses) const;
        bool                             m_AllowSeparateComputeFamily = false;
        VriPipelineCacheInterface        m_PipelineCacheApi {};
        std::vector<DeferredRelease>     m_DeferredReleases;
    };

    // One submission in flight per Frame. Queue waits/signals use caller-owned VRI fences.
    class Frame
    {
    public:
        explicit Frame(Device& device);
        static std::expected<std::unique_ptr<Frame>, std::string>
        create(Device& device, VriQueueType queueType = VriQueueType_Graphics);
        ~Frame();
        Frame(const Frame&)                                                   = delete;
        Frame&                                        operator=(const Frame&) = delete;
        VriCommandBuffer*                             begin();
        void                                          submitAndWait();
        std::expected<VriCommandBuffer*, std::string> beginExpected();
        // Nonblocking submission. Borrowed fences and resources survive their final queued GPU use.
        // An ordinary queue rejection keeps the ended recording available for a submission retry.
        std::expected<void, std::string> submitExpected(std::span<const VriFenceSubmitDesc> waits   = {},
                                                        std::span<const VriFenceSubmitDesc> signals = {});
        // Opt in to an independent completion owner. Rejection publishes no credential; discard/restore as usual.
        std::expected<SubmissionCompletion, std::string>
        submitWithCompletionExpected(std::span<const VriFenceSubmitDesc> waits   = {},
                                     std::span<const VriFenceSubmitDesc> signals = {});
        // A timeout leaves the submission pending; callers can wait again.
        std::expected<void, std::string> waitExpected(uint64_t timeoutNs = UINT64_MAX);
        // Discard commands known to be unsubmitted. Device loss leaves the Frame unusable.
        // Callers restore CPU-side resource state only after discard succeeds.
        std::expected<void, std::string> discardExpected();
        std::expected<void, std::string> submitAndWaitExpected();

        // Stable VRI handle for routing and validation. Call beginExpected() before recording commands.
        VriCommandBuffer* recordingDestination() const
        {
            return m_Commands;
        }

        VriQueueType queueType() const
        {
            return m_QueueType;
        }

        double waitMs() const
        {
            return m_WaitMs;
        }

    private:
        friend class RenderGraph;

        struct Uninitialized
        {};

        Frame(Device& device, Uninitialized);
        std::expected<void, std::string> initialize(VriQueueType queueType);
        std::expected<void, std::string> resetAllocatorExpected();
        std::expected<void, std::string> validateSubmissionExpected(std::span<const VriFenceSubmitDesc> waits,
                                                                    std::span<const VriFenceSubmitDesc> signals,
                                                                    uint32_t internalSignals) const;
        void                             release() noexcept;

        enum class State
        {
            eIdle,
            eRecording,
            eExecutable,
            eSubmitted,
            eInvalid,
            eDeviceLost
        };
        Device&                         m_Device;
        VriQueue*                       m_Queue     = nullptr;
        VriQueueType                    m_QueueType = VriQueueType_Graphics;
        State                           m_State     = State::eIdle;
        VriCommandAllocator*            m_Allocator = nullptr;
        VriCommandBuffer*               m_Commands  = nullptr;
        VriFence*                       m_Fence     = nullptr;
        uint64_t                        m_Value     = 0;
        uint64_t                        m_RecordingSerial    = 0;
        bool                            m_RecordingDiscarded = false;
        double                          m_WaitMs    = 0;
        std::vector<VriFenceSubmitDesc> m_SubmitSignals;
    };
} // namespace vultra
