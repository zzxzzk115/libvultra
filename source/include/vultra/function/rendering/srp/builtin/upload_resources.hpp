#pragma once

#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/rendering/framework/prepared_render_data.hpp"
#include "vultra/function/rendering/render_structs.hpp"

namespace vultra
{
    struct alignas(16) GPUFrameBlock
    {
        uint32_t frameIndex {0};
        float    time {0.0f};
        float    deltaTime {0.0f};
        float    padding0 {0.0f};
    };
    static_assert(sizeof(GPUFrameBlock) % 16 == 0, "GPUFrameBlock must be 16-byte aligned");

    struct alignas(16) GPUCameraBlock
    {
        GPUCameraBlock() = default;
        GPUCameraBlock(const rhi::Extent2D extent, const RenderCamera& camera);

        glm::mat4 projection {1.0f};
        glm::mat4 inverseProjection {1.0f};
        glm::mat4 view {1.0f};
        glm::mat4 inverseView {1.0f};
        glm::mat4 viewProjection {1.0f};
        glm::mat4 inverseViewProjection {1.0f};

        glm::vec4 resolution {1.0f, 1.0f, 1.0f, 1.0f};

        float zNear {0.1f};
        float zFar {1000.0f};
        float fovY {0.0f};
        float padding0 {0.0f};

        glm::vec4 frustumPlanes[6] {};
    };
    static_assert(sizeof(GPUCameraBlock) % 16 == 0, "GPUCameraBlock must be 16-byte aligned");

    [[nodiscard]] GPUFrameBlock  makeGPUFrameBlock(uint64_t frameIndex, float time = 0.0f, float deltaTime = 0.0f);
    [[nodiscard]] GPUCameraBlock makeGPUCameraBlock(rhi::Extent2D extent, const RenderCamera& camera);

    template<typename Uploader>
    void prepareFrameData(Uploader&        uploader,
                          FrameRenderData& out,
                          uint64_t         frameIndex,
                          float            time      = 0.0f,
                          float            deltaTime = 0.0f)
    {
        out.frameIndex           = frameIndex;
        out.frameData.frameBlock = uploader.uploadStruct("UploadFrameBlock",
                                                         "FrameBlock",
                                                         framegraph::BufferType::eUniformBuffer,
                                                         makeGPUFrameBlock(frameIndex, time, deltaTime));
    }

    template<typename Uploader>
    void
    prepareCameraData(Uploader& uploader, ViewRenderData& out, const rhi::Extent2D extent, const RenderCamera& camera)
    {
        out.cameraData.cameraBlock = uploader.uploadStruct("UploadCameraBlock",
                                                           "CameraBlock",
                                                           framegraph::BufferType::eUniformBuffer,
                                                           makeGPUCameraBlock(extent, camera));
    }
} // namespace vultra
