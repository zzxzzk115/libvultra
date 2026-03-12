#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/function/framegraph/framegraph_data_registry.hpp"
#include "vultra/function/rendering/framework/prepared_render_data.hpp"

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
        const FrameRenderData&  frame;
        const ViewRenderData&   viewData;

        [[nodiscard]] const RenderView& view() const { return viewData.view; }
    };

    struct FrameGraphExecContext
    {
        rhi::CommandBuffer&    cb;
        rhi::RenderDevice&     rd;
        const FrameRenderData& frame;
        ViewRenderData         viewData;
        ResourceSet            resourceSet;

        struct Extra
        {
            rhi::ShaderLibraryRuntime* builtinShaderLib {nullptr};
            Samplers                   samplers;
        } ext;

        [[nodiscard]] const RenderView&                   view() const { return viewData.view; }
        [[nodiscard]] std::optional<rhi::FramebufferInfo> framebufferInfo() const { return viewData.framebufferInfo; }

        void bindDescriptorSets(const rhi::BasePipeline& pipeline)
        {
            for (const auto& [set, bindings] : resourceSet)
            {
                auto descriptorSetBuilder = cb.createDescriptorSetBuilder();
                for (const auto& [index, info] : bindings)
                    descriptorSetBuilder.bind(index, info);
                const auto descriptors = descriptorSetBuilder.build(pipeline.getDescriptorSetLayout(set));
                cb.bindDescriptorSet(set, descriptors);
            }
        }

        void clear() { resourceSet.clear(); }

        inline static void overrideSampler(rhi::ResourceBinding& v, const vk::Sampler sampler)
        {
            assert(sampler);
            if (std::holds_alternative<rhi::bindings::CombinedImageSampler>(v))
                std::get<rhi::bindings::CombinedImageSampler>(v).sampler = sampler;
        }
    };
} // namespace vultra

#define PASS_SETUP_ZONE ZoneScopedN("SetupPass")
