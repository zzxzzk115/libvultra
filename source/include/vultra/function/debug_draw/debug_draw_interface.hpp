#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <debug_draw.hpp>
#include <glm/mat4x4.hpp>

#include <optional>

using namespace dd;

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
    } // namespace rhi

    class DebugDrawInterface final : public RenderInterface
    {
    public:
        void initialize(rhi::RenderDevice& renderDevice, rhi::PixelFormat colorFormat);

        void setViewProjectionMatrix(const glm::mat4& matrix);
        void overrideArea(rhi::Rect2D area);
        void bindDepthTexture(rhi::Texture& depthTexture);

        void beginFrame(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& framebufferInfo);
        void endFrame();

        virtual GlyphTextureHandle createGlyphTexture(int width, int height, const void* pixels) override;
        virtual void               destroyGlyphTexture(GlyphTextureHandle glyphTex) override;

        virtual void drawPointList(const DrawVertex* points, int count, bool depthEnabled) override;
        virtual void drawLineList(const DrawVertex* lines, int count, bool depthEnabled) override;
        virtual void drawGlyphList(const DrawVertex* glyphs, int count, GlyphTextureHandle glyphTex) override;

    private:
        rhi::RenderDevice* m_RenderDevice {nullptr};
        rhi::PixelFormat   m_ColorFormat {rhi::PixelFormat::eUndefined};

        glm::mat4                  m_ViewProjectionMatrix {1.0f};
        std::optional<rhi::Rect2D> m_OverrideArea;
        rhi::Texture*              m_DepthTexture {nullptr};

        rhi::GraphicsPipeline m_LineGraphicsPipeline;
        rhi::VertexBuffer     m_VertexBuffer;
        rhi::CommandBuffer*   m_CurrentCommandBuffer {nullptr};
    };
} // namespace vultra