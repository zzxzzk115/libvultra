#pragma once

#include "vultra/function/framegraph/framegraph_context.hpp"

#include <string>

namespace vultra
{
    struct XrViewSynthesisSettings
    {
        std::string warpingBackend {"adaptive_mesh_graphics"};
        std::string inpaintingBackend {"pull_push"};
        std::string sourceView {"left"};
        std::string targetView {"right"};

        uint32_t baseGridSize {16};
        uint32_t maxSubdivision {2};
        float    sideLengthThreshold {0.12f};
        float    depthThreshold {0.02f};
    };

    class XrViewSynthesisPass final
    {
    public:
        XrViewSynthesisPass() = default;

        [[nodiscard]] FrameGraphResource addPass(FrameGraphBuildContext&            ctx,
                                                 FrameGraphResource                 source,
                                                 FrameGraphResource                 depth,
                                                 const XrViewSynthesisSettings& settings);
    };
} // namespace vultra
