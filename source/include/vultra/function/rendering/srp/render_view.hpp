#pragma once

#include "vultra/function/rendering/render_structs.hpp"

#include <array>
#include <glm/vec4.hpp>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    struct RenderView
    {
        const RenderWorld*  renderWorld {nullptr};
        const RenderCamera* camera {nullptr};

        rhi::Texture*                      target {nullptr};
        rhi::Extent2D                      extent {};
        glm::vec4                          clearValue {0, 0, 0, 1};
        bool                               enableMultiview {false};
        uint32_t                           multiviewMask {0};
        std::array<const RenderCamera*, 2> multiviewCameras {nullptr, nullptr};
        uint32_t                           multiviewCameraCount {0};

        resource::GpuSceneDatabase* gpuSceneDatabase {nullptr};
        resource::GpuSceneView*     gpuSceneView {nullptr};
    };
} // namespace vultra
