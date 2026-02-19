#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/render_context.hpp"
#include "vultra/function/rendering/render_camera.hpp"
#include "vultra/function/world/world.hpp"

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

        World&        world;
        RenderCamera& camera;

        fsec dt;
    };
} // namespace vultra
