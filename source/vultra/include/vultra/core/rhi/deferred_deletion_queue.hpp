#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        // Process-wide queue that defers GPU-resource destruction until the GPU is guaranteed to no
        // longer reference the resource.
        //
        // RHI resource destructors (buffers, textures, pipelines) enqueue their vkDestroy*/
        // vmaDestroy* call here instead of executing it immediately. This prevents destroying a
        // resource that is still bound by an in-flight - or currently-recording - command buffer,
        // which the Vulkan validation layer reports as "VkBuffer/VkImage/VkPipeline ... is currently
        // in use by VkCommandBuffer ..." and which manifests as ErrorDeviceLost. The classic trigger
        // in this engine is an editor scene switch: it runs inside renderFrame() (via the ImGui
        // callback) and tears down the previous scene's GPU resources mid-frame.
        //
        // Enqueueing is thread-safe (resources may be destroyed on async asset-load worker threads);
        // the deferred destroy callbacks are always executed on the thread that drains the queue
        // (the render thread, at frame begin, or on waitIdle/teardown). Assumes a single active
        // RenderDevice, which holds for libvultra.
        class DeferredDeletionQueue
        {
        public:
            [[nodiscard]] static DeferredDeletionQueue& get();

            // Enqueue a destroy callback, tagged with the current frame. No-op if empty.
            void enqueue(std::function<void()> deleter);

            // Advance the frame counter and run every callback old enough that the GPU has finished
            // all frames that could still reference it. Call once per frame at frame begin.
            void beginFrame();

            // Run all pending callbacks immediately. Call after a GPU waitIdle, and before the
            // render device / allocator are themselves destroyed (the callbacks capture allocator /
            // device handles that must still be valid).
            void flushAll();

        private:
            DeferredDeletionQueue() = default;

            std::mutex                                              m_Mutex;
            std::vector<std::pair<std::uint64_t, std::function<void()>>> m_Pending;
            std::uint64_t                                           m_Frame {0};
        };
    } // namespace rhi
} // namespace vultra
