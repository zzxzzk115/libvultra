#pragma once

#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/function/rendering/framework/uploaded_buffer.hpp"
#include "vultra/function/rendering/srp/render_view.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace vultra
{
    struct FrameData
    {
        UploadedBuffer frameBlock;
    };

    struct CameraData
    {
        UploadedBuffer cameraBlock;
        UploadedBuffer stereoCameraBlock;
    };

    struct StereoViewData
    {
        StereoRenderMode mode {StereoRenderMode::eMono};
        uint32_t         viewCount {1};
        glm::vec4        leftFov {0.0f};
        glm::vec4        rightFov {0.0f};
        glm::vec3        headPosition {0.0f};
        float            ipd {0.0f};
        int64_t          predictedDisplayTime {0};
        bool             positionValid {false};
        bool             orientationValid {false};
        bool             positionTracked {false};
        bool             orientationTracked {false};
    };

    struct FrameRenderData
    {
        uint64_t  frameIndex {0};
        FrameData frameData;
    };

    struct ViewRenderData
    {
        RenderView view;
        CameraData cameraData;
        StereoViewData stereoViewData;

        std::optional<rhi::FramebufferInfo> framebufferInfo;
    };
} // namespace vultra
