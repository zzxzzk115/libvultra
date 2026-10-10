#pragma once

#include <vultra/core/math/render_camera.hpp>
#include <vultra/drivers/openxr/openxr.hpp>
#include <vultra/scene/camera/fps_camera.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>
#include <vultra/servers/rendering/research/headset_profile.hpp>

#include <array>

namespace vultra
{
    struct StereoFrameViews
    {
        std::array<RenderCamera, 3> cameras;
        uint64_t                    index = 0;
    };

    using StereoOutputs = std::array<RenderGraph::Resource, 2>;

    StereoFrameViews makeStereoFrameViews(const FpsCamera&      rig,
                                          const XRFrame&        frame,
                                          std::array<Extent, 2> sizes,
                                          float                 ipd,
                                          uint64_t              index,
                                          const HeadsetProfile* profile      = nullptr,
                                          const glm::mat4&      trackingPose = glm::mat4(1));
    StereoFrameViews makeStereoFrameViews(const RenderCamera&   rig,
                                          const XRFrame&        frame,
                                          std::array<Extent, 2> sizes,
                                          float                 ipd,
                                          uint64_t              index,
                                          const HeadsetProfile* profile      = nullptr,
                                          const glm::mat4&      trackingPose = glm::mat4(1));
} // namespace vultra
