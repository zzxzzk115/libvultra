#pragma once

#include "vultra/core/rhi/framebuffer_info.hpp"
#include "vultra/function/rendering/framework/uploaded_buffer.hpp"
#include "vultra/function/rendering/srp/render_view.hpp"

namespace vultra
{
    struct FrameData
    {
        UploadedBuffer frameBlock;
    };

    struct CameraData
    {
        UploadedBuffer cameraBlock;
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

        std::optional<rhi::FramebufferInfo> framebufferInfo;
    };
} // namespace vultra
