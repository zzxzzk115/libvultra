#pragma once
#include <vultra/servers/rendering/shader_material.hpp>

namespace vultra::detail
{
    std::expected<std::string, std::string> shaderPipelineKeyExpected(const ShaderAsset&       asset,
                                                                      const MaterialInstance&  instance,
                                                                      const ShaderPass&        pass,
                                                                      const ShaderPassContext& context);
    std::expected<void, std::string>        applyShaderStateExpected(const ShaderAsset&                   asset,
                                                                     const MaterialInstance&              instance,
                                                                     const ShaderState&                   state,
                                                                     const ShaderPassContext&             context,
                                                                     VriGraphicsPipelineDesc&             pipeline,
                                                                     std::vector<VriColorAttachmentDesc>& colors);
} // namespace vultra::detail
