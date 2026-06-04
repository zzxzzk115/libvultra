#pragma once

#include "vultra/function/framegraph/framegraph_context.hpp"

#include <glm/mat4x4.hpp>

namespace vultra
{
    // Flushes the global dd:: immediate-mode debug queue (commonContext.debugDraw) over the scene color
    // target, depth-testing against the supplied scene depth so wireframes are occluded by geometry.
    class DebugDrawPass final
    {
    public:
        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      source,
                                   FrameGraphResource      depth,
                                   const glm::mat4&        viewProjection);
    };
} // namespace vultra
