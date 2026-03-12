#include "vultra/function/rendering/srp/builtin/upload_resources.hpp"

#include <glm/geometric.hpp>

namespace vultra
{
    namespace
    {
        [[nodiscard]] glm::vec4 normalizePlane(const glm::vec4 p)
        {
            const float len = glm::length(glm::vec3(p));
            return len > 0.0f ? p / len : p;
        }
    } // namespace

    GPUCameraBlock::GPUCameraBlock(const rhi::Extent2D extent, const RenderCamera& camera) :
        projection(camera.projection), inverseProjection(camera.inverseProjection), view(camera.view),
        inverseView(camera.inverseView), viewProjection(camera.viewProjection),
        inverseViewProjection(camera.inverseViewProjection),
        resolution(static_cast<float>(extent.width),
                   static_cast<float>(extent.height),
                   extent.width > 0 ? 1.0f / static_cast<float>(extent.width) : 0.0f,
                   extent.height > 0 ? 1.0f / static_cast<float>(extent.height) : 0.0f),
        zNear(camera.zNear), zFar(camera.zFar), fovY(camera.fovY)
    {
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
