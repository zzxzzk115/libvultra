#pragma once

#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/framebuffer_info.hpp"

#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        class BasePipeline;
        class GraphicsPipeline;
        class CommandBuffer;
    } // namespace rhi

    namespace framegraph
    {
        using ResourceBindings = std::unordered_map<rhi::BindingIndex, rhi::ResourceBinding>;
        using ResourceSet      = std::unordered_map<rhi::DescriptorSetIndex, ResourceBindings>;
        using Samplers         = std::unordered_map<std::string, vk::Sampler>;

        class RenderContext
        {
        public:
            explicit RenderContext(rhi::CommandBuffer& commandBuffer, Samplers& samplers) :
                commandBuffer {commandBuffer}, samplers {samplers}
            {}
            RenderContext(const RenderContext&) = delete;
            virtual ~RenderContext()            = default;

            rhi::CommandBuffer&                 commandBuffer;
            std::optional<rhi::FramebufferInfo> framebufferInfo;
            ResourceSet                         resourceSet;
            Samplers&                           samplers;
        };
    } // namespace framegraph
} // namespace vultra

#define PASS_SETUP_ZONE ZoneScopedN("SetupPass")