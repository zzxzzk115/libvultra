#include "vultra/function/rendering/srp/builtin/android_compat_renderer.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/shader_service.hpp"

#include <vshadersystem/engine_keywords.hpp>

namespace vultra
{
    void AndroidCompatRenderer::init()
    {
        auto* services = getServices();
        if (!services)
            return;

        auto& shaderService = services->require<IShaderService>();
        auto& shaderLib     = shaderService.builtinLibrary();

        auto vertexVariantHash =
            shaderLib.computeVariantHash("android_compat.vert", vshadersystem::ShaderStage::eVert, {});
        auto vertexShader = shaderLib.load(vertexVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            m_LastError = "Failed to load android_compat.vert";
            VULTRA_CORE_ERROR("[AndroidCompatRenderer] {}", m_LastError);
            return;
        }

        auto fragmentVariantHash =
            shaderLib.computeVariantHash("android_compat.frag", vshadersystem::ShaderStage::eFrag, {});
        auto fragmentShader = shaderLib.load(fragmentVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            m_LastError = "Failed to load android_compat.frag";
            VULTRA_CORE_ERROR("[AndroidCompatRenderer] {}", m_LastError);
            return;
        }

        m_VertexSpirv   = vertexShader->spirv;
        m_FragmentSpirv = fragmentShader->spirv;
        m_ShaderLoaded  = true;

        auto& backendService = services->require<IRenderBackendService>();
        m_RenderDevice       = &backendService.renderDevice();
    }

    const rhi::GraphicsPipeline* AndroidCompatRenderer::getPipeline(const rhi::PixelFormat colorFormat)
    {
        if (!m_RenderDevice || !m_ShaderLoaded)
            return nullptr;

        if (auto it = m_PipelineCache.find(colorFormat); it != m_PipelineCache.end())
            return &it->second;

        auto pipeline = rhi::GraphicsPipeline::Builder {}
                            .setColorFormats({colorFormat})
                            .setInputAssembly({})
                            .addBuiltinShader(rhi::ShaderType::eVertex, m_VertexSpirv)
                            .addBuiltinShader(rhi::ShaderType::eFragment, m_FragmentSpirv)
                            .setDepthStencil({
                                .depthTest  = false,
                                .depthWrite = false,
                            })
                            .setRasterizer({
                                .polygonMode = rhi::PolygonMode::eFill,
                                .cullMode    = rhi::CullMode::eNone,
                            })
                            .setBlending(0, {.enabled = false})
                            .build(*m_RenderDevice);

        if (!pipeline)
        {
            VULTRA_CORE_ERROR("[AndroidCompatRenderer] Failed to build pipeline for color format {}",
                              static_cast<uint32_t>(colorFormat));
            return nullptr;
        }

        auto [it, inserted] = m_PipelineCache.emplace(colorFormat, std::move(pipeline));
        (void)inserted;
        return &it->second;
    }

    void AndroidCompatRenderer::render(ImmediateRenderContext& ctx)
    {
        if (!m_RenderDevice || !m_ShaderLoaded || !ctx.viewData.framebufferInfo)
            return;

        auto& framebufferInfo = *ctx.viewData.framebufferInfo;
        if (framebufferInfo.colorAttachments.empty() || !framebufferInfo.colorAttachments[0].target)
            return;

        const auto& target   = *framebufferInfo.colorAttachments[0].target;
        const auto* pipeline = getPipeline(target.getPixelFormat());
        if (!pipeline)
            return;

        ctx.cb.beginRendering(framebufferInfo);
        ctx.cb.bindPipeline(*pipeline).drawFullScreenTriangle();
        ctx.cb.endRendering();
        ctx.clear();
    }

    void AndroidCompatRenderer::onImGui()
    {
        // Intentionally minimal for now.
    }
} // namespace vultra
