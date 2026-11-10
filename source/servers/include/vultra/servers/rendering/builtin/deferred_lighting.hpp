#pragma once

#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/gbuffer_pipeline.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <array>

namespace vultra
{
    class DeferredLightingPipeline
    {
    public:
        static std::expected<std::unique_ptr<DeferredLightingPipeline>, std::string>
        create(Device&                 device,
               VriPipelineLayout*      layout,
               const ShaderProgram&    sky,
               const ShaderProgram&    lighting,
               bool                    clustered        = false,
               GBufferPositionEncoding positionEncoding = GBufferPositionEncoding::eWorld32);
        DeferredLightingPipeline(const DeferredLightingPipeline&)            = delete;
        DeferredLightingPipeline& operator=(const DeferredLightingPipeline&) = delete;

        bool belongsTo(const Device& device) const
        {
            return &m_Device == &device;
        }

        bool clustered() const
        {
            return m_Clustered;
        }

        GBufferPositionEncoding positionEncoding() const
        {
            return m_PositionEncoding;
        }

        VriPipelineLayout* layout() const
        {
            return m_Layout;
        }

        VriPipeline* sky() const
        {
            return m_Sky->handle();
        }

        VriPipeline* lighting() const
        {
            return m_Lighting->handle();
        }

    private:
        DeferredLightingPipeline(Device&                 device,
                                 VriPipelineLayout*      layout,
                                 bool                    clustered,
                                 GBufferPositionEncoding positionEncoding) :
            m_Device(device),
            m_Layout(layout),
            m_Clustered(clustered),
            m_PositionEncoding(positionEncoding)
        {
        }

        Device&                         m_Device;
        VriPipelineLayout*              m_Layout;
        bool                            m_Clustered;
        GBufferPositionEncoding         m_PositionEncoding;
        std::unique_ptr<ShaderPipeline> m_Sky;
        std::unique_ptr<ShaderPipeline> m_Lighting;
    };

    struct DeferredLightingGraphInputs
    {
        RenderGraph::Resource                frame {};
        RenderGraph::Resource                hdr {};
        RenderGraph::Resource                clusters {};
        Buffer*                              clusterBuffer = nullptr;
        std::array<RenderGraph::Resource, 7> gbuffer {};
        // Frame texture bindings 1..9 and 11, in that order. Repeated resources are allowed.
        std::array<RenderGraph::Resource, 10> lightingTextures {};
        std::array<const VriDescriptor*, 12>  lighting {};
        VriDescriptorSet*                     frameSet = nullptr;
    };

    // The frame set must bind the supplied frame and lighting descriptors. A clustered shader also requires
    // the exact cluster buffer at set 0, binding 20; the graph resource must import that same buffer.
    // Keep the pipeline, descriptor set and referenced resources alive through execution; discard the graph
    // if adding either pass fails.
    std::expected<void, std::string> addDeferredLightingGraphPasses(RenderGraph&                    graph,
                                                                    const DeferredLightingPipeline& pipeline,
                                                                    std::string                     name,
                                                                    DeferredLightingGraphInputs     inputs);

    // After compilation and before recording any commands using the frame set, bind the G-buffer views.
    // Reapply after rebinding resources, and only after prior submissions using the set have completed.
    // Restore these bindings before destroying the graph if the set will be reused afterwards.
    std::expected<void, std::string>
    prepareDeferredLightingFrame(RenderGraph&                                graph,
                                 const std::array<RenderGraph::Resource, 7>& gbuffer,
                                 VriDescriptorSet*                           frameSet,
                                 GBufferPositionEncoding positionEncoding = GBufferPositionEncoding::eWorld32);
} // namespace vultra
