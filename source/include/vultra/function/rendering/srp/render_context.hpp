#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/render_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    using ResourceBindings = std::unordered_map<rhi::BindingIndex, rhi::ResourceBinding>;
    using ResourceSet      = std::unordered_map<rhi::DescriptorSetIndex, ResourceBindings>;

    // Single-parameter render context passed across Renderer/Features/Passes.
    struct RenderContext
    {
        rhi::CommandBuffer&                 cb;
        rhi::RenderDevice&                  rd;
        std::optional<rhi::FramebufferInfo> framebufferInfo;
        ResourceSet                         resourceSet;

        FrameGraph&           fg;
        FrameGraphBlackboard& bb;

        RenderWorld&  renderWorld;
        RenderCamera& camera;

        fsec dt;

        void bindDescriptorSets(const rhi::BasePipeline& pipeline)
        {
            auto descriptorSetBuilder = cb.createDescriptorSetBuilder();
            for (const auto& [set, bindings] : resourceSet)
            {
                for (const auto& [index, info] : bindings)
                {
                    descriptorSetBuilder.bind(index, info);
                }
                const auto descriptors = descriptorSetBuilder.build(pipeline.getDescriptorSetLayout(set));
                cb.bindDescriptorSet(set, descriptors);
            }
        }
    };
} // namespace vultra
