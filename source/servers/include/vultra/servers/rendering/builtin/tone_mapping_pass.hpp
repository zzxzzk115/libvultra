#pragma once

#include <vultra/drivers/rhi/shader_bindings.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/graph/pass_catalog.hpp>

#include <array>
#include <optional>

namespace vultra
{
    // The renderer and graph catalog share this stage. UNORM receives sRGB; float output stays linear for XR.
    class ToneMappingPass final : public GraphPass
    {
    public:
        static std::expected<std::unique_ptr<ToneMappingPass>, std::string>
        create(Device& device, VriFormat format = VriFormat_RGBA8_UNORM, const AssetSource* shaders = nullptr);
        ~ToneMappingPass() override;
        std::expected<std::vector<RenderGraph::Resource>, std::string>
                        addPassesExpected(RenderGraph&                           graph,
                                          std::string_view                       name,
                                          std::span<const RenderGraph::Resource> inputs,
                                          std::span<const double>                parameters) override;
        ShaderPipeline& shader();

    private:
        ToneMappingPass(Device& device, VriFormat format);
        std::expected<void, std::string> initializeExpected(const AssetSource* shaders);
        std::expected<VriPipeline*, std::string> buildExpected(const ShaderProgram&           program,
                                                               std::span<const VriShaderDesc> shaders);
        Device&                         m_Device;
        VriFormat                       m_Format;
        ShaderResourceViews                      m_Input {"hdrColor", VriDescriptorType_Texture, {nullptr}};
        std::optional<ShaderUniformBlock>        m_Constants;
        std::array<float, 4>                     m_LastValues {};
        std::unique_ptr<ShaderBindings>          m_Bindings;
        // Destroy the pipeline before its binding layout.
        std::unique_ptr<ShaderPipeline> m_Pipeline;
    };

    PassDefinition toneMappingDefinition();
} // namespace vultra
