#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/renderer/builtin/ui_structs.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class UIPass final : public rhi::RenderPass<UIPass>
        {
            friend class BasePass;

        public:
            explicit UIPass(rhi::RenderDevice&);

            void draw(rhi::CommandBuffer& cb, const std::vector<UIDrawCommand>& commands);

        private:
            rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat, UIDrawCommand::Type type) const;
        };
    } // namespace gfx
} // namespace vultra