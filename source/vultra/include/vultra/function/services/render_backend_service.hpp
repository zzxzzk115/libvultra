#pragma once

#include "vultra/core/rhi/frame_controller.hpp"
#include "vultra/core/rhi/interfaces/iimgui.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/swapchain.hpp"

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <vbase/service/service_registry.hpp>

#include <cstdint>
#include <span>

namespace vultra
{
    class IRenderBackendService
    {
    public:
        struct XREyeView
        {
            uint32_t      eyeIndex {0};
            glm::mat4     view {1.0f};
            glm::mat4     projection {1.0f};
            glm::mat4     pose {1.0f};
            glm::vec4     fov {0.0f};
            glm::vec3     headPosition {0.0f};
            glm::quat     headRotation {1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3     eyePosition {0.0f};
            glm::quat     eyeRotation {1.0f, 0.0f, 0.0f, 0.0f};
            float         ipd {0.0f};
            int64_t       predictedDisplayTime {0};
            bool          positionValid {false};
            bool          orientationValid {false};
            bool          positionTracked {false};
            bool          orientationTracked {false};
            rhi::Extent2D extent {};
            rhi::Texture* target {nullptr};
            rhi::Texture* stereoTarget {nullptr};
            rhi::Texture* mirrorTarget {nullptr};
        };

        SERVICE_REGISTER(IRenderBackendService)

        virtual rhi::RenderDevice& renderDevice() = 0;

        virtual rhi::Swapchain& swapchain() = 0;

        virtual rhi::FrameController& frameController() = 0;
        virtual rhi::IImGui&    imguiBackend() = 0;

        virtual bool beginFrame() = 0;

        virtual rhi::CommandBuffer& commandBuffer() = 0;

        virtual rhi::Texture& backbuffer() = 0;

        virtual bool isXREnabled() const       = 0;
        virtual bool isXRMirrorEnabled() const = 0;
        virtual bool isExitRequested() const   = 0;
        virtual void requestXRSession(bool requested) = 0;

        virtual std::span<const XREyeView> xrEyeViews() const = 0;
        virtual std::span<const XREyeView> lastXREyeViews() const = 0;

        virtual void endFrame() = 0;

        virtual void present() = 0;
    };
} // namespace vultra
