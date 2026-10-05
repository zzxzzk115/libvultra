#pragma once

#include <vultra/core/math/render_camera.hpp>
#include <vultra/servers/rendering/builtin/environment.hpp>
#include <vultra/servers/rendering/builtin/render_light.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <memory>
#include <span>

namespace vultra
{
    class ShaderPipeline;

    // Progressive opaque OpenPBR reference integrator. Requires VRI ray query and bindless textures/samplers.
    // One sample per pixel/frame; Russian roulette terminates paths without a fixed depth or radiance clamp.
    class ReferencePathTracer
    {
    public:
        struct Outputs
        {
            RenderGraph::Resource radiance; // RGBA32F history; averaged linear radiance.
            RenderGraph::Resource hdr;      // RGBA16F scene color for existing processing and tone mapping.
            RenderGraph::Resource albedo;
            RenderGraph::Resource normal; // World-space shading normal; signed XYZ.
            RenderGraph::Resource depth;  // Positive camera view depth; zero for a miss.
            RenderGraph::Resource motion; // Previous minus current unjittered pixel position, top-left origin.
            RenderGraph::Resource sampleCount;
            RenderGraph::Resource rayCount; // Cumulative traced rays per pixel since the last reset.
        };

        ReferencePathTracer(Device& device, GpuScene& scene, Environment& environment);
        ~ReferencePathTracer();
        ReferencePathTracer(const ReferencePathTracer&)            = delete;
        ReferencePathTracer& operator=(const ReferencePathTracer&) = delete;

        Outputs addPasses(RenderGraph& graph, Extent size);
        // Previous GPU use must be complete; the camera must use a pinhole perspective projection.
        // Scene, camera, lighting, environment, seed and shader changes reset history.
        void            prepare(const RenderCamera&          camera,
                                RenderGraph&                 graph,
                                const Outputs&               outputs,
                                std::span<const RenderLight> lights,
                                float                        environmentIntensity,
                                uint32_t                     seed);
        void            completeFrame(); // Call after submitting and completing the prepared frame.
        uint32_t        samples() const;
        ShaderPipeline& shader(); // Reload/poll only between completed frames; a new generation resets history.

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
