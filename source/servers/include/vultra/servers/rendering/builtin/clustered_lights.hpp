#pragma once

#include <vultra/core/math/render_camera.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>
#include <vultra/servers/rendering/builtin/render_light.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

namespace vultra
{
    // Three-dimensional screen/depth light masks for visibility shading. Every cluster has 64 bits,
    // exactly matching the renderer's maximum light count; no per-cluster list can overflow.
    // Keep this instance alive until its graph and GPU work finish. One frame may be in flight.
    class ClusteredLights
    {
    public:
        static constexpr uint32_t kTileSize    = 16;
        static constexpr uint32_t kDepthSlices = 24;
        static std::expected<std::unique_ptr<ClusteredLights>, std::string>
        create(Device& device, const ShaderProgram& program, Extent extent);
        ~ClusteredLights();
        ClusteredLights(const ClusteredLights&)                                             = delete;
        ClusteredLights&                                  operator=(const ClusteredLights&) = delete;
        std::expected<RenderGraph::Resource, std::string> addToGraph(RenderGraph& graph, std::string name);
        std::expected<void, std::string> setFrame(const RenderCamera& camera, std::span<const RenderLight> lights);
        Buffer&                          buffer() const;
        Extent                           extent() const;

    private:
        struct Impl;
        explicit ClusteredLights(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
