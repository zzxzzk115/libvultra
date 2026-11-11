#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/graph/pass_catalog.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <expected>
#include <memory>

namespace vultra
{
    // The renderer and graph catalog share this stage. UNORM receives sRGB; float output stays linear for XR.
    class ToneMappingPass final : public GraphPass
    {
    public:
        // Recorded passes and the pipeline builder keep this pass's address, so it lives behind a unique_ptr.
        [[nodiscard]] static std::expected<std::unique_ptr<ToneMappingPass>, Error>
        create(Device& device, VriFormat format = VriFormat_RGBA8_UNORM, const AssetSource* shaders = nullptr);
        ~ToneMappingPass() override;
        ToneMappingPass(const ToneMappingPass&)            = delete;
        ToneMappingPass& operator=(const ToneMappingPass&) = delete;
        std::expected<std::vector<RenderGraph::Resource>, Error>
                        addPasses(RenderGraph&                           graph,
                                  std::string_view                       name,
                                  std::span<const RenderGraph::Resource> inputs,
                                  std::span<const double>                parameters) override;
        ShaderPipeline& shader();

    private:
        ToneMappingPass(Device& device, VriFormat format);

        Device&                         m_Device;
        VriFormat                       m_Format;
        VriPipelineLayout*              m_Layout = nullptr;
        VriDescriptorPool*              m_Pool   = nullptr;
        VriDescriptorSet*               m_Set    = nullptr;
        std::unique_ptr<ShaderPipeline> m_Pipeline;
    };

    PassDefinition toneMappingDefinition();
} // namespace vultra
