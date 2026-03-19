#include "vultra/function/rendering/srp/builtin/upload_resources.hpp"

#include <glm/geometric.hpp>

namespace vultra
{
    GPUCameraBlock::GPUCameraBlock(const rhi::Extent2D extent, const RenderCamera& camera) :
        view(camera.view), inverseView(camera.inverseView), zNear(camera.zNear), zFar(camera.zFar), fovY(camera.fovY)
    {
        // recalculate projection to avoid aspect ratio issues
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        projection         = glm::perspective(camera.fovY, aspect, camera.zNear, camera.zFar);
        projection[1][1] *= -1.0f; // Vulkan clip space adjustment
        inverseProjection     = glm::inverse(projection);
        viewProjection        = projection * view;
        inverseViewProjection = glm::inverse(viewProjection);
        resolution            = glm::vec4(extent.width, extent.height, 1.0f / extent.width, 1.0f / extent.height);

        for (int i = 0; i < 6; ++i)
            frustumPlanes[i] = camera.frustumPlanes[i];
    }

    GPUFrameBlock makeGPUFrameBlock(uint64_t frameIndex, float time, float deltaTime)
    {
        return GPUFrameBlock {
            .frameIndex = static_cast<uint32_t>(frameIndex),
            .time       = time,
            .deltaTime  = deltaTime,
            .padding0   = 0.0f,
        };
    }

    GPUCameraBlock makeGPUCameraBlock(rhi::Extent2D extent, const RenderCamera& camera)
    {
        return GPUCameraBlock {extent, camera};
    }
} // namespace vultra
