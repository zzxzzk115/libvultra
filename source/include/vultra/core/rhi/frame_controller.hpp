#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/rendertarget_view.hpp"

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class Swapchain;

        // Simplifies usage of frames in flight.
        class FrameController final
        {
        public:
            FrameController() = default;
            FrameController(RenderDevice&, Swapchain&, const FrameIndex::ValueType numFramesInFlight);
            FrameController(const FrameController&) = delete;
            FrameController(FrameController&&) noexcept;
            ~FrameController();

            FrameController& operator=(const FrameController&) = delete;
            FrameController& operator=(FrameController&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] FrameIndex::ValueType size() const;
            [[nodiscard]] RenderTargetView      getCurrentTarget() const;

            CommandBuffer&   beginFrame();
            bool             acquireNextFrame();
            FrameController& endFrame();

            void present();

            void recreate();

        private:
            void create(const FrameIndex::ValueType numFramesInFlight);
            void destroy() noexcept;

        private:
            RenderDevice* m_RenderDevice {nullptr};
            Swapchain*    m_Swapchain {nullptr};

            struct PerFrameData
            {
                CommandBuffer commandBuffer;
                vk::Semaphore imageAcquired {nullptr};
                vk::Semaphore renderCompleted {nullptr};
            };
            std::vector<PerFrameData> m_Frames;
            FrameIndex                m_FrameIndex;

            bool m_ImageAcquired {false};
        };
    } // namespace rhi
} // namespace vultra