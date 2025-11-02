#pragma once

#include "vultra/core/rhi/render_pass.hpp"

#include <fg/Fwd.hpp>
#include <glm/mat4x4.hpp>

namespace vultra
{
    namespace gfx
    {
        enum class DebugDrawMode
        {
            eNone = 0,
            eLine
        };

        class DebugDrawPass final : public rhi::RenderPass<DebugDrawPass>
        {
            friend class BasePass;

        public:
            explicit DebugDrawPass(rhi::RenderDevice&);

            void addPass(FrameGraph&, FrameGraphBlackboard&, const fsec, const glm::mat4& viewProjectionMatrix);
        };
    } // namespace gfx
} // namespace vultra
