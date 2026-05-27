#pragma once

#include "vultra/function/rendering/render_structs.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <glm/vec4.hpp>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    enum class StereoRenderMode : uint32_t
    {
        eMono = 0,
        eSingleGraphStereo,
        ePerEyeFallback,
    };

    struct RenderView
    {
        const RenderWorld*  renderWorld {nullptr};
        const RenderCamera* camera {nullptr};

        rhi::Texture*                      target {nullptr};
        rhi::Extent2D                      extent {};
        glm::vec4                          clearValue {0, 0, 0, 1};
        StereoRenderMode                   stereoMode {StereoRenderMode::eMono};
        bool                               enableMultiview {false};
        uint32_t                           multiviewMask {0};
        std::array<const RenderCamera*, 2> multiviewCameras {nullptr, nullptr};
        uint32_t                           multiviewCameraCount {0};

        resource::GpuSceneDatabase* gpuSceneDatabase {nullptr};
        resource::GpuSceneView*     gpuSceneView {nullptr};

        [[nodiscard]] uint32_t renderTargetViewMask() const
        {
            return enableMultiview ? multiviewMask : 0u;
        }

        [[nodiscard]] uint32_t renderTargetLayerCount() const
        {
            const auto viewMask = renderTargetViewMask();
            if (viewMask == 0u)
                return 1u;
            return std::max(1u, static_cast<uint32_t>(std::popcount(viewMask)));
        }

        [[nodiscard]] bool usesSingleGraphStereo() const
        {
            return stereoMode == StereoRenderMode::eSingleGraphStereo && renderTargetViewMask() != 0u;
        }
    };
} // namespace vultra
