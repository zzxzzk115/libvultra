#pragma once

#include "vultra/function/framegraph/render_context.hpp"
#include "vultra/function/renderer/batch.hpp"
#include "vultra/function/renderer/texture_resources.hpp"

namespace vultra
{
    namespace gfx
    {
        class RendererRenderContext final : public framegraph::RenderContext
        {
        public:
            explicit RendererRenderContext(rhi::CommandBuffer& commandBuffer, framegraph::Samplers& samplers);

            // TODO: Batch rendering
            void render(const rhi::GraphicsPipeline&, const Batch&);
            void bindBatch(const Batch&);
            void drawBatch(const Batch&);

            void bindMaterialTextures(const TextureResources&);
            void bindDescriptorSets(const rhi::BasePipeline&);
            void renderFullScreenPostProcess(const rhi::BasePipeline&);
            void endRendering();

            // Helpers

            [[nodiscard]] static std::string toString(const framegraph::ResourceSet&);
            static void                      overrideSampler(rhi::ResourceBinding&, const vk::Sampler);
        };
    } // namespace gfx
} // namespace vultra