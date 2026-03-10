#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/framebuffer_info.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/function/framegraph/framegraph_data_registry.hpp"
#include "vultra/function/rendering/srp/render_view.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    struct FrameGraphBuildContext
    {
        FrameGraph&           fg;
        FrameGraphBlackboard& bb;

        rhi::RenderDevice&      rd;
        FrameGraphDataRegistry& data;
        const RenderView&       view;
    };

    struct FrameGraphExecContext
    {
        rhi::CommandBuffer&                 cb;
        rhi::RenderDevice&                  rd;
        std::optional<rhi::FramebufferInfo> framebufferInfo;
        ResourceSet                         resourceSet;
        const RenderView&                   view;

        struct Extra
        {
            rhi::ShaderLibraryRuntime* builtinShaderLib {nullptr};
        } ext;

        void bindDescriptorSets(const rhi::BasePipeline& pipeline)
        {
            for (const auto& [set, bindings] : resourceSet)
            {
                auto descriptorSetBuilder = cb.createDescriptorSetBuilder();
                for (const auto& [index, info] : bindings)
                {
                    descriptorSetBuilder.bind(index, info);
                }
                const auto descriptors = descriptorSetBuilder.build(pipeline.getDescriptorSetLayout(set));
                cb.bindDescriptorSet(set, descriptors);
            }
        }

        void clear()
        {
            framebufferInfo.reset();
            resourceSet.clear();
        }
    };
} // namespace vultra

#define PASS_SETUP_ZONE ZoneScopedN("SetupPass")
