#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/core/base/ranges.hpp"
#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/command_buffer.hpp"

#include <format>
#include <sstream>

namespace vultra
{
    namespace gfx
    {
        namespace
        {
            void bindTextures(framegraph::ResourceBindings& bindings, const TextureResources& textures)
            {
                bindings.clear();

                for (const auto& [i, textureInfo] : vultra::enumerate(textures | std::views::values))
                {
                    bindings[static_cast<uint32_t>(i)] = rhi::bindings::CombinedImageSampler {
                        .texture     = textureInfo.texture.get(),
                        .imageAspect = rhi::ImageAspect::eColor,
                    };
                }
            }
            void changeOffset(rhi::ResourceBinding& v, const uint32_t offset)
            {
                std::get<rhi::bindings::StorageBuffer>(v).offset = offset;
            }

            void bindDescriptorSets_(rhi::CommandBuffer&            cb,
                                     const rhi::BasePipeline&       pipeline,
                                     const framegraph::ResourceSet& sets)
            {
                auto descriptorSetBuilder = cb.createDescriptorSetBuilder();
                for (const auto& [set, bindings] : sets)
                {
                    for (const auto& [index, info] : bindings)
                    {
                        descriptorSetBuilder.bind(index, info);
                    }
                    const auto descriptors = descriptorSetBuilder.build(pipeline.getDescriptorSetLayout(set));
                    cb.bindDescriptorSet(set, descriptors);
                }
            }

            [[nodiscard]] auto validate(const gfx::TextureResources& textures)
            {
                return std::ranges::all_of(textures, [](const auto& p) { return p.second.isValid(); });
            }

        } // namespace

        RendererRenderContext::RendererRenderContext(rhi::CommandBuffer&   commandBuffer,
                                                     framegraph::Samplers& samplers) :
            RenderContext(commandBuffer, samplers)
        {
            resourceSet.reserve(4);
        }

        void RendererRenderContext::render(const rhi::GraphicsPipeline&, const Batch&) {}

        void RendererRenderContext::bindBatch(const Batch&) {}

        void RendererRenderContext::drawBatch(const Batch&) {}

        void RendererRenderContext::bindMaterialTextures(const TextureResources& textures)
        {
            bindTextures(resourceSet[3], textures);
        }

        void RendererRenderContext::bindDescriptorSets(const rhi::BasePipeline& pipeline)
        {
            bindDescriptorSets_(commandBuffer, pipeline, resourceSet);
        }

        void RendererRenderContext::renderFullScreenPostProcess(const rhi::BasePipeline& pipeline)
        {
            auto& cb = commandBuffer;
            RHI_GPU_ZONE(cb, "FullScreenPostProcess");
            cb.bindPipeline(pipeline);
            bindDescriptorSets(pipeline);
            cb.beginRendering(*framebufferInfo).drawFullScreenTriangle();
            endRendering();
        }

        void RendererRenderContext::endRendering()
        {
            commandBuffer.endRendering();
            framebufferInfo.reset();
            resourceSet.clear();
        }

        void RendererRenderContext::endRayTracing() { resourceSet.clear(); }

        std::string RendererRenderContext::toString(const framegraph::ResourceSet& sets)
        {
            std::ostringstream oss;
            for (const auto& [set, bindings] : sets)
            {
                for (const auto& [index, info] : bindings)
                {
                    std::ostream_iterator<std::string> {oss, "\n"} =
                        std::format("[set={}, binding={}] = {}", set, index, rhi::toString(info));
                }
            }
            return oss.str();
        }

        void RendererRenderContext::overrideSampler(rhi::ResourceBinding& v, const vk::Sampler sampler)
        {
            assert(sampler);

            if (std::holds_alternative<rhi::bindings::CombinedImageSampler>(v))
            {
                std::get<rhi::bindings::CombinedImageSampler>(v).sampler = sampler;
            }
            else
            {
                throw std::runtime_error("Invalid type in ResourceBinding");
            }
        }
    } // namespace gfx
} // namespace vultra