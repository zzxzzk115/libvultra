#pragma once

#include <cstdint>
#include <string>

namespace vultra
{
    // Shared parameters for the view-synthesis reprojection passes (GeometryWarp,
    // PullPushInpaint). These are not XR-specific: any source->target reprojection
    // (stereo eye synthesis, temporal reprojection, FOV change) drives the same passes.
    //
    // Defaults match pixelwise-viewpoint-warping GraphicsWarpingSettings/InpaintingSettings
    // (gridSize 1 = per-pixel mesh, sideLenThreshold 0.01, useDepthAware true,
    // depthThreshold 0.01).
    struct ViewSynthesisSettings
    {
        bool        enabled {true};
        std::string sourceView {"left"};
        std::string targetView {"right"};

        // Geometry warp: grid cell size in pixels, and the warped-edge NDC length above
        // which a stretched primitive is treated as a disocclusion hole.
        uint32_t gridSize {1};
        float    sideLenThreshold {0.01f};

        // Shared depth-aware mode (warp hole classification + depth-aware pull-push) and
        // the pull-push depth-difference selection threshold.
        bool  useDepthAware {true};
        float depthThreshold {0.01f};
    };
} // namespace vultra
