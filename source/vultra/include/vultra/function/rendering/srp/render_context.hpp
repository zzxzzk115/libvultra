#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/rendering/framework/prepared_render_data.hpp"

namespace vultra
{
    struct ImmediateRenderContext
    {
        rhi::CommandBuffer&    cb;
        rhi::RenderDevice&     rd;
        const FrameRenderData& frame;
        const ViewRenderData&  viewData;

        ResourceSet resourceSet;

        [[nodiscard]] const RenderView& view() const { return viewData.view; }

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
    };
} // namespace vultra
