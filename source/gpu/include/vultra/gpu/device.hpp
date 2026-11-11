#pragma once
#include <vultra/core/base/error.hpp>

#include <vri/ext/vri_ext_pipeline_cache.h>
#include <vri/vri.h>

#include <array>
#include <expected>
#include <memory>
#include <vector>

namespace vultra
{
    [[nodiscard]] inline std::expected<void, Error> check(VriResult result, const char* operation)
    {
        if (result != VriResult_Success)
        {
            return makeError("{} failed: {}", operation, int(result));
        }
        return {};
    }

    // VRI remains the public rendering API. This class only owns the device and its tables. Frames, resources
    // and swapchains keep references to it, so it lives behind a unique_ptr and never moves.
    class Device
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<Device>, Error> create(const VriDeviceCreationDesc& desc);
        // Convenience creation preserves the builtin shaders' Y-up authoring convention.
        // Strict feature requirements by default; bestEffort enables explicit capability-based selection.
        [[nodiscard]] static std::expected<std::unique_ptr<Device>, Error>
        create(bool        validation       = true,
               const void* nativeCreateInfo = nullptr,
               uint64_t    features         = 0,
               bool        bestEffort       = false);
        [[nodiscard]] static std::expected<std::unique_ptr<Device>, Error>
        create(VriGraphicsAPI api,
               bool           validation       = true,
               const void*    nativeCreateInfo = nullptr,
               uint64_t       features         = 0,
               bool           bestEffort       = false);
        ~Device();
        Device(const Device&)                                             = delete;
        Device&                                  operator=(const Device&) = delete;
        [[nodiscard]] std::expected<void, Error> waitIdle() const;
        // Destructors report completion errors through the log.
        void waitIdleForDestruction() const noexcept;

        VriDevice* handle   = nullptr;
        uint64_t   features = 0;
        VriQueue*  queue    = nullptr;
        // A queue distinct from `queue` for asynchronous compute work; null when the device has none.
        VriQueue*             computeQueue = nullptr;
        VriCoreInterface      core {};
        VriSwapChainInterface swap {};
        // Pass this device-owned cache to graphics/compute VRI descriptors.
        VriPipelineCache* pipelineCache = nullptr;

    private:
        Device() = default;
        static std::expected<std::unique_ptr<Device>, Error> create(const VriDeviceCreationDesc& requested,
                                                                    bool                         automaticSelection);
        std::expected<void, Error>                           initialize();
        VriPipelineCacheInterface                            m_PipelineCacheApi {};
    };

    enum class FrameQueue
    {
        eGraphics,
        eCompute
    };

    // One frame in flight. submitAndWait is the lifetime boundary for all resources.
    // Most frames record one graphics command buffer between begin() and submitAndWait(). A frame may also be
    // split into several submissions on the graphics and compute queues; each submission signals a point on
    // its queue's timeline that a submission on the other queue can wait for.
    class Frame
    {
    public:
        // Frames are referenced by recorders while they are open, so they live behind a unique_ptr.
        [[nodiscard]] static std::expected<std::unique_ptr<Frame>, Error> create(Device& device);
        ~Frame();
        Frame(const Frame&)            = delete;
        Frame& operator=(const Frame&) = delete;
        // Starts the frame and returns its first graphics command buffer.
        [[nodiscard]] std::expected<VriCommandBuffer*, Error> begin();
        // The open command buffer of this queue, starting a new one after a submit(). Using the compute queue
        // on a device without a separate one is an error.
        [[nodiscard]] std::expected<VriCommandBuffer*, Error> commands(FrameQueue queue);
        // Submits the open command buffer of this queue and returns the timeline point it signals.
        // A nonzero waitOther delays it until the other queue reaches that point.
        [[nodiscard]] std::expected<uint64_t, Error> submit(FrameQueue queue, uint64_t waitOther = 0);
        // The final graphics submission of this frame waits for this compute point.
        void waitForCompute(uint64_t point);
        // Submits the open graphics command buffer and waits until every submission of this frame completed.
        [[nodiscard]] std::expected<void, Error> submitAndWait();

        double waitMs() const
        {
            return m_WaitMs;
        }

    private:
        struct Queue
        {
            VriQueue*                      queue     = nullptr;
            VriCommandAllocator*           allocator = nullptr;
            std::vector<VriCommandBuffer*> buffers;
            size_t                         used  = 0;
            VriCommandBuffer*              open  = nullptr;
            VriFence*                      fence = nullptr;
            uint64_t                       value = 0;
        };

        explicit Frame(Device& device);
        std::expected<Queue*, Error> state(FrameQueue queue);

        Device&              m_Device;
        std::array<Queue, 2> m_Queues;
        uint64_t             m_ComputeWait = 0;
        double               m_WaitMs      = 0;
    };
} // namespace vultra
