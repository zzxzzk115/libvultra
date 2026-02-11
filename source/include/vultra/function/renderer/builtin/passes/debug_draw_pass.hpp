#pragma once

#include "vultra/core/rhi/render_pass.hpp"

#include <fg/Fwd.hpp>
#include <glm/mat4x4.hpp>

namespace vultra
{
    class DebugDrawInterface;

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
            explicit DebugDrawPass(rhi::RenderDevice&, DebugDrawInterface& debugDrawInterface);

            void addPass(FrameGraph&, FrameGraphBlackboard&, const fsec, const glm::mat4& viewProjectionMatrix);

        private:
            DebugDrawInterface& m_DebugDrawInterface;
        };
    } // namespace gfx
} // namespace vultra
