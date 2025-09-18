#include "vultra/function/renderer/builtin/post_process_helper.hpp"

#include <shader_headers/fullscreen_triangle.vert.spv.h>

namespace vultra
{
    namespace gfx
    {
        rhi::GraphicsPipeline createPostProcessPipelineFromSPV(rhi::RenderDevice&     rd,
                                                               const rhi::PixelFormat colorFormat,
                                                               const rhi::SPIRV&      spv)
        {
            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({colorFormat})
                .setInputAssembly({})
                .addBuiltinShader(rhi::ShaderType::eVertex, fullscreen_triangle_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, spv)
                .setDepthStencil({
                    .depthTest  = false,
                    .depthWrite = false,
                })
                .setRasterizer({
                    .polygonMode = rhi::PolygonMode::eFill,
                    .cullMode    = rhi::CullMode::eFront,
                })
                .setBlending(0, {.enabled = false})
                .build(rd);
        }
    } // namespace gfx
} // namespace vultra