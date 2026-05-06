#include "vultra/function/rendering/srp/builtin/upload_resources.hpp"

#include <glm/geometric.hpp>
#include <algorithm>

namespace vultra
{
    GPUCameraBlock::GPUCameraBlock(const rhi::Extent2D      extent,
                                   const RenderCamera&      camera,
                                   const rhi::RenderBackendApi backendApi) :
        view(camera.view), inverseView(camera.inverseView), zNear(camera.zNear), zFar(camera.zFar), fovY(camera.fovY)
    {
        const float safeWidth  = static_cast<float>(std::max<uint32_t>(extent.width, 1u));
        const float safeHeight = static_cast<float>(std::max<uint32_t>(extent.height, 1u));
        projection             = camera.projection;
        if (backendApi == rhi::RenderBackendApi::eVulkan)
        {
            projection[1][1] *= -1.0f; // Vulkan clip space adjustment
        }
        inverseProjection     = glm::inverse(projection);
        viewProjection        = projection * view;
        inverseViewProjection = glm::inverse(viewProjection);
        resolution            = glm::vec4(safeWidth, safeHeight, 1.0f / safeWidth, 1.0f / safeHeight);

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

    GPUCameraBlock
    makeGPUCameraBlock(rhi::Extent2D extent, const RenderCamera& camera, const rhi::RenderBackendApi backendApi)
    {
        return GPUCameraBlock {extent, camera, backendApi};
    }

    GPUStereoCameraBlock
    makeGPUStereoCameraBlock(rhi::Extent2D extent,
                             const RenderCamera& left,
                             const RenderCamera* right,
                             const rhi::RenderBackendApi backendApi)
    {
        GPUStereoCameraBlock block {};
        block.cameras[0] = makeGPUCameraBlock(extent, left, backendApi);
        block.cameras[1] = makeGPUCameraBlock(extent, right ? *right : left, backendApi);
        return block;
    }
} // namespace vultra
