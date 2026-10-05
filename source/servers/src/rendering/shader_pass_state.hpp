#pragma once
#include <vultra/servers/rendering/shader_material.hpp>

namespace vultra::detail
{
    std::string shaderPipelineKey(const ShaderAsset&       asset,
                                  const MaterialInstance&  instance,
                                  const ShaderPass&        pass,
                                  const ShaderPassContext& context);
    void        applyShaderState(const ShaderAsset&                   asset,
                                 const MaterialInstance&              instance,
                                 const ShaderState&                   state,
                                 const ShaderPassContext&             context,
                                 VriGraphicsPipelineDesc&             pipeline,
                                 std::vector<VriColorAttachmentDesc>& colors);
} // namespace vultra::detail
