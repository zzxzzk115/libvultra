#pragma once

#include "vultra/core/rhi/frame_controller.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/swapchain.hpp"

#include <vbase/service/service_registry.hpp>

namespace vultra
{
    class IRenderBackendService
    {
    public:
        SERVICE_REGISTER(IRenderBackendService)

        virtual rhi::RenderDevice& renderDevice() = 0;

        virtual rhi::Swapchain& swapchain() = 0;

        virtual rhi::FrameController& frameController() = 0;

        virtual bool beginFrame() = 0;

        virtual rhi::CommandBuffer& commandBuffer() = 0;

        virtual rhi::Texture& backbuffer() = 0;

        virtual void endFrame() = 0;

        virtual void present() = 0;
    };
} // namespace vultra
