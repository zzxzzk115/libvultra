#pragma once
#include <vri/ext/vri_ext_pipeline_cache.h>
#include <vri/vri.h>

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace vultra
{
    inline void check(VriResult result, const char* operation)
    {
        if (result != VriResult_Success)
        {
            throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
        }
    }

    // VRI remains the public rendering API. This class only owns the device and its tables.
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
        ~Device();
        Device(const Device&)            = delete;
        Device& operator=(const Device&) = delete;
        void    waitIdle() const;
        // Destructors report completion errors without throwing during stack unwinding.
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
        Device(const VriDeviceCreationDesc& requested, bool automaticSelection);
        VriPipelineCacheInterface m_PipelineCacheApi {};
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
        explicit Frame(Device& device);
        ~Frame();
        Frame(const Frame&)            = delete;
        Frame& operator=(const Frame&) = delete;
        // Starts the frame and returns its first graphics command buffer.
        VriCommandBuffer* begin();
        // The open command buffer of this queue, starting a new one after a submit().
        VriCommandBuffer* commands(FrameQueue queue);
        // Submits the open command buffer of this queue and returns the timeline point it signals.
        // A nonzero waitOther delays it until the other queue reaches that point.
        uint64_t submit(FrameQueue queue, uint64_t waitOther = 0);
        // The final graphics submission of this frame waits for this compute point.
        void waitForCompute(uint64_t point);
        // Submits the open graphics command buffer and waits until every submission of this frame completed.
        void submitAndWait();

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

        Queue& state(FrameQueue queue);

        Device&              m_Device;
        std::array<Queue, 2> m_Queues;
        uint64_t             m_ComputeWait = 0;
        double               m_WaitMs      = 0;
    };
} // namespace vultra
