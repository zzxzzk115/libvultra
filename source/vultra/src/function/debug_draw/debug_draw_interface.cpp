#include "vultra/function/debug_draw/debug_draw_interface.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include "builtin_shaders.hpp"

#define DEBUG_DRAW_IMPLEMENTATION
#include <debug_draw.hpp>

using namespace dd; // contained to this translation unit

namespace vultra
{
    void DebugDrawInterface::initialize(rhi::RenderDevice& renderDevice, rhi::PixelFormat colorFormat)
    {
        m_RenderDevice = &renderDevice;
        m_ColorFormat  = colorFormat;
        m_VertexBuffer = renderDevice.createVertexBuffer(sizeof(DrawVertex), 4 * 1024 * 1024 / sizeof(DrawVertex));
        m_ShaderLibrary.loadFromMemory(builtin_shaders_highend_vshlib, builtin_shaders_highend_vshlib_size);

        m_NeedsPipelineRebuild = true; // Lazy-build
    }

    void DebugDrawInterface::setViewProjectionMatrix(const glm::mat4& matrix) { m_ViewProjectionMatrix = matrix; }

    void DebugDrawInterface::overrideArea(rhi::Rect2D area) { m_OverrideArea = area; }

    void DebugDrawInterface::updateColorFormat(rhi::PixelFormat colorFormat)
    {
        if (m_ColorFormat != colorFormat)
        {
            m_ColorFormat          = colorFormat;
            m_NeedsPipelineRebuild = true;
        }
    }

    void DebugDrawInterface::bindDepthTexture(rhi::Texture* depthTexture)
    {

        if (m_DepthTexture != depthTexture)
        {
            m_DepthTexture         = depthTexture;
            m_NeedsPipelineRebuild = true;
        }
    }

    void DebugDrawInterface::setDepthTest(rhi::PixelFormat depthFormat)
    {
        if (m_DepthTestFormat != depthFormat)
        {
            m_DepthTestFormat      = depthFormat;
            m_NeedsPipelineRebuild = true;
        }
    }

    void DebugDrawInterface::buildPipelineIfNeeded()
    {
        if (!m_NeedsPipelineRebuild)
            return;

        auto builder = rhi::GraphicsPipeline::Builder {};
        builder.setColorFormats({m_ColorFormat});

        const rhi::PixelFormat depthFormat =
            m_DepthTexture ? m_DepthTexture->getPixelFormat() : m_DepthTestFormat;
        if (depthFormat != rhi::PixelFormat::eUndefined)
        {
            // Depth-test against the scene depth but do not write (debug overlay must not corrupt depth).
            builder.setDepthFormat(depthFormat).setDepthStencil({.depthTest = true, .depthWrite = false});
        }

        const auto vertexHash =
            rhi::ShaderLibraryRuntime::computeVariantHash("debug_draw.vert", vshadersystem::ShaderStage::eVert, {});
        const auto fragmentHash =
            rhi::ShaderLibraryRuntime::computeVariantHash("debug_draw.frag", vshadersystem::ShaderStage::eFrag, {});
        const auto vertexShader   = m_ShaderLibrary.load(vertexHash, vshadersystem::ShaderStage::eVert);
        const auto fragmentShader = m_ShaderLibrary.load(fragmentHash, vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[DebugDraw] Missing debug draw shader in builtin_highend.vshlib");
            return;
        }

        m_LineGraphicsPipeline = builder
                                     .setInputAssembly([] {
                                         rhi::VertexAttributes attrs;
                                         attrs[0] = rhi::VertexAttribute {0, rhi::VertexAttribute::Type::eFloat3, 0};
                                         attrs[1] = rhi::VertexAttribute {1, rhi::VertexAttribute::Type::eFloat3, 12};
                                         attrs[2] = rhi::VertexAttribute {2, rhi::VertexAttribute::Type::eFloat, 24};
                                         return attrs;
                                     }())
                                     .setVertexStride(sizeof(DrawVertex))
                                     .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
                                     .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
                                     .setBlending(0, {.enabled = false})
                                     .setTopology(rhi::PrimitiveTopology::eLineList)
                                     .build(*m_RenderDevice);

        m_NeedsPipelineRebuild = false;
    }

    void DebugDrawInterface::beginFrame(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& framebufferInfo)
    {
        m_CurrentCommandBuffer = &cb;

        auto fbInfo = framebufferInfo;
        if (m_OverrideArea)
        {
            fbInfo.area = *m_OverrideArea;
        }
        if (m_DepthTexture && !fbInfo.depthAttachment)
        {
            fbInfo.depthAttachment = {.target = m_DepthTexture};
        }

        buildPipelineIfNeeded();

        cb.bindPipeline(m_LineGraphicsPipeline).beginRendering(fbInfo);
    }

    void DebugDrawInterface::endFrame()
    {
        m_CurrentCommandBuffer->endRendering();
        m_CurrentCommandBuffer = nullptr;
    }

    GlyphTextureHandle DebugDrawInterface::createGlyphTexture(int width, int height, const void* pixels)
    {
        return GlyphTextureHandle {};
    }

    void DebugDrawInterface::destroyGlyphTexture(GlyphTextureHandle glyphTex) {}

    void DebugDrawInterface::drawPointList(const DrawVertex* points, int count, bool depthEnabled)
    {
        (void)points;
        (void)count;
        (void)depthEnabled;
    }

    void DebugDrawInterface::drawLineList(const DrawVertex* lines, int count, bool depthEnabled)
    {
        if (count == 0)
            return;

        const size_t dataSize = count * sizeof(DrawVertex);

        if (dataSize > m_VertexBuffer.getSize())
        {
            m_VertexBuffer = m_RenderDevice->createVertexBuffer(sizeof(DrawVertex), count);
        }

        m_RenderDevice->uploadS(m_VertexBuffer, 0, dataSize, lines);

        m_CurrentCommandBuffer->pushConstants(rhi::ShaderStages::eVertex, 0, &m_ViewProjectionMatrix)
            .draw({
                .vertexBuffer = &m_VertexBuffer,
                .numVertices  = static_cast<uint32_t>(count),
            });
    }

    void DebugDrawInterface::drawGlyphList(const DrawVertex* glyphs, int count, GlyphTextureHandle glyphTex)
    {
        (void)glyphs;
        (void)count;
        (void)glyphTex;
    }
} // namespace vultra
