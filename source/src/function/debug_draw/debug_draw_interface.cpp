#include "vultra/function/debug_draw/debug_draw_interface.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include "shader_headers/debug_draw.frag.spv.h"
#include "shader_headers/debug_draw.vert.spv.h"

#define DEBUG_DRAW_IMPLEMENTATION
#include <debug_draw.hpp>

#include <iostream>

namespace vultra
{
    void DebugDrawInterface::initialize(rhi::RenderDevice& renderDevice, rhi::PixelFormat colorFormat)
    {
        m_RenderDevice = &renderDevice;
        m_ColorFormat  = colorFormat;

        // No depth test/write, no blending, default pipeline
        m_LineGraphicsPipeline = rhi::GraphicsPipeline::Builder {}
                                     .setColorFormats({colorFormat})
                                     .setInputAssembly({
                                         {0, {.type = rhi::VertexAttribute::Type::eFloat3, .offset = 0}},
                                         {1, {.type = rhi::VertexAttribute::Type::eFloat3, .offset = 12}},
                                         {2, {.type = rhi::VertexAttribute::Type::eFloat, .offset = 24}},
                                     })
                                     .addBuiltinShader(rhi::ShaderType::eVertex, debug_draw_vert_spv)
                                     .addBuiltinShader(rhi::ShaderType::eFragment, debug_draw_frag_spv)
                                     .setDepthStencil({.depthTest = false, .depthWrite = false})
                                     .setBlending(0, {.enabled = false})
                                     .setTopology(rhi::PrimitiveTopology::eLineList)
                                     .build(renderDevice);

        m_VertexBuffer = renderDevice.createVertexBuffer(sizeof(DrawVertex), 4 * 1024 * 1024 / sizeof(DrawVertex));
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

    void DebugDrawInterface::buildPipelineIfNeeded()
    {
        if (!m_NeedsPipelineRebuild)
            return;

        auto builder = rhi::GraphicsPipeline::Builder {};
        builder.setColorFormats({m_ColorFormat});

        if (m_DepthTexture)
        {
            builder.setDepthFormat(m_DepthTexture->getPixelFormat())
                .setDepthStencil({.depthTest = true, .depthWrite = true});
        }

        m_LineGraphicsPipeline = builder
                                     .setInputAssembly({
                                         {0, {.type = rhi::VertexAttribute::Type::eFloat3, .offset = 0}},
                                         {1, {.type = rhi::VertexAttribute::Type::eFloat3, .offset = 12}},
                                         {2, {.type = rhi::VertexAttribute::Type::eFloat, .offset = 24}},
                                     })
                                     .addBuiltinShader(rhi::ShaderType::eVertex, debug_draw_vert_spv)
                                     .addBuiltinShader(rhi::ShaderType::eFragment, debug_draw_frag_spv)
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
        cb.beginRendering(fbInfo);
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
        std::cout << "DebugDrawInterface::drawPointList called with " << count
                  << " points, depthEnabled = " << depthEnabled << std::endl;
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

        auto staging = m_RenderDevice->createStagingBuffer(dataSize, lines);
        m_RenderDevice->execute(
            [&](auto& cb) { cb.copyBuffer(staging, m_VertexBuffer, vk::BufferCopy {0, 0, dataSize}); });

        m_CurrentCommandBuffer->bindPipeline(m_LineGraphicsPipeline)
            .pushConstants(rhi::ShaderStages::eVertex, 0, &m_ViewProjectionMatrix)
            .draw({
                .vertexBuffer = &m_VertexBuffer,
                .numVertices  = static_cast<uint32_t>(count),
            });
    }

    void DebugDrawInterface::drawGlyphList(const DrawVertex* glyphs, int count, GlyphTextureHandle glyphTex)
    {
        std::cout << "DebugDrawInterface::drawGlyphList called with " << count << " glyphs" << std::endl;
    }
} // namespace vultra