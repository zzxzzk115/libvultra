#pragma once

#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/graph/pass_catalog.hpp>

namespace vultra
{
    // The renderer and graph catalog share this stage. UNORM receives sRGB; float output stays linear for XR.
    class ToneMappingPass final : public GraphPass
    {
    public:
        explicit ToneMappingPass(Device& device, VriFormat format = VriFormat_RGBA8_UNORM);
        ~ToneMappingPass() override;
        std::vector<RenderGraph::Resource> addPasses(RenderGraph&                           graph,
                                                     std::string_view                       name,
                                                     std::span<const RenderGraph::Resource> inputs,
                                                     std::span<const double>                parameters) override;
        ShaderPipeline&                    shader();

    private:
        void                            release();
        Device&                         m_Device;
        VriFormat                       m_Format;
        VriPipelineLayout*              m_Layout = nullptr;
        VriDescriptorPool*              m_Pool   = nullptr;
        VriDescriptorSet*               m_Set    = nullptr;
        std::unique_ptr<ShaderPipeline> m_Pipeline;
    };

    PassDefinition toneMappingDefinition();
} // namespace vultra
