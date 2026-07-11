#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/base_pass.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/function/framegraph/framegraph_data_registry.hpp"
#include "vultra/function/rendering/framework/prepared_render_data.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>
#include <vbase/core/scope_exit.hpp>

#include <utility>
#include <variant>
#include <vector>

namespace vultra
{
    class RenderFrameResources;

    struct FrameGraphBuildContext
    {
        FrameGraph&           fg;
        FrameGraphBlackboard& bb;

        rhi::RenderDevice&      rd;
        FrameGraphDataRegistry& data;
        RenderFrameResources*   frameResources {nullptr};
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
            rhi::ShaderLibraryRuntime* builtinHighendShaderLib {nullptr};
            rhi::ShaderLibraryRuntime* builtinCompatibilityShaderLib {nullptr};
            rhi::ShaderLibraryRuntime* projectShaderLib {nullptr};
            Samplers                   samplers;

            [[nodiscard]] rhi::ShaderLibraryRuntime* builtinShaderLibForProfile(const rhi::ShaderProfile profile) const
            {
                switch (profile)
                {
                    case rhi::ShaderProfile::eHighend:
                        return builtinHighendShaderLib ? builtinHighendShaderLib : builtinShaderLib;
                    case rhi::ShaderProfile::eCompatibility:
                        return builtinCompatibilityShaderLib ? builtinCompatibilityShaderLib : builtinShaderLib;
                    case rhi::ShaderProfile::eGeneral:
                    case rhi::ShaderProfile::eUnspecified:
                    default:
                        return builtinShaderLib;
                }
            }
        } ext;

        [[nodiscard]] const RenderView&                   view() const { return viewData.view; }
        [[nodiscard]] std::optional<rhi::FramebufferInfo> framebufferInfo() const { return viewData.framebufferInfo; }

        void bindDescriptorSet(const rhi::BasePipeline& pipeline, const rhi::DescriptorSetIndex set)
        {
            const auto setIt = resourceSet.find(set);
            if (setIt == resourceSet.end())
                return;

            auto descriptorSetBuilder = cb.createDescriptorSetBuilder();
            for (const auto& [index, info] : setIt->second)
                descriptorSetBuilder.bind(index, info);
            const auto descriptors = descriptorSetBuilder.build(pipeline.getDescriptorSetLayout(set));
            cb.bindDescriptorSet(set, descriptors);
        }

        void bindDescriptorSets(const rhi::BasePipeline& pipeline)
        {
            for (const auto& [set, _] : resourceSet)
                bindDescriptorSet(pipeline, set);
        }

        // (set, binding) combined-image-sampler slots that bind a depth texture this frame, taken from the
        // framegraph's eDepth reads already materialized into resourceSet. A pass passes this to its pipeline
        // builder so WebGPU declares those slots unfilterable-float (a depth view can't bind to filterable
        // float). Single source of truth is the read's imageAspect - no per-pass hardcoded binding numbers.
        [[nodiscard]] std::vector<std::pair<uint32_t, uint32_t>> collectDepthSampledBindings() const
        {
            std::vector<std::pair<uint32_t, uint32_t>> result;
            for (const auto& [set, bindings] : resourceSet)
            {
                for (const auto& [binding, info] : bindings)
                {
                    if (const auto* cis = std::get_if<rhi::bindings::CombinedImageSampler>(&info);
                        cis && cis->imageAspect == rhi::ImageAspect::eDepth)
                    {
                        result.emplace_back(static_cast<uint32_t>(set), static_cast<uint32_t>(binding));
                    }
                }
            }
            return result;
        }

        void clear()
        {
            resourceSet.clear();
            viewData.framebufferInfo = std::nullopt;
        }

        inline static void overrideSampler(rhi::ResourceBinding& v, const rhi::Sampler sampler)
        {
            assert(sampler);
            if (std::holds_alternative<rhi::bindings::CombinedImageSampler>(v))
                std::get<rhi::bindings::CombinedImageSampler>(v).sampler = sampler;
        }
    };

    inline auto makeScopedFrameGraphExecContextClear(FrameGraphExecContext& rc) noexcept
    {
        return vbase::scope_exit([&rc] { rc.clear(); });
    }
} // namespace vultra

#define PASS_SETUP_ZONE ZoneScopedN("SetupPass")
#define VULTRA_FRAMEGRAPH_EXEC_CONTEXT_JOIN_(a, b) VULTRA_FRAMEGRAPH_EXEC_CONTEXT_JOIN_INNER_(a, b)
#define VULTRA_FRAMEGRAPH_EXEC_CONTEXT_JOIN_INNER_(a, b) a##b
#define VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(name, ctxPtr) \
    auto& name = *static_cast<::vultra::FrameGraphExecContext*>(ctxPtr); \
    auto  VULTRA_FRAMEGRAPH_EXEC_CONTEXT_JOIN_(_frameGraphExecContextScope_, __LINE__) = \
        ::vultra::makeScopedFrameGraphExecContextClear(name)
