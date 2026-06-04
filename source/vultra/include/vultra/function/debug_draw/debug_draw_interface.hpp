#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <debug_draw.hpp>
#include <glm/mat4x4.hpp>

#include <optional>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
    } // namespace rhi

    class DebugDrawInterface final : public dd::RenderInterface
    {
    public:
        void initialize(rhi::RenderDevice& renderDevice, rhi::PixelFormat colorFormat);

        void setViewProjectionMatrix(const glm::mat4& matrix);
        void overrideArea(rhi::Rect2D area);
        void updateColorFormat(rhi::PixelFormat colorFormat);
        void bindDepthTexture(rhi::Texture* depthTexture);
        // Enables depth testing (read-only) against a depth attachment supplied via the FramebufferInfo
        // (e.g. a frame-graph depth resource). Pass eUndefined to disable.
        void setDepthTest(rhi::PixelFormat depthFormat);
        void buildPipelineIfNeeded();

        void beginFrame(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& framebufferInfo);
        void endFrame();

        virtual dd::GlyphTextureHandle createGlyphTexture(int width, int height, const void* pixels) override;
        virtual void                   destroyGlyphTexture(dd::GlyphTextureHandle glyphTex) override;

        virtual void drawPointList(const dd::DrawVertex* points, int count, bool depthEnabled) override;
        virtual void drawLineList(const dd::DrawVertex* lines, int count, bool depthEnabled) override;
        virtual void drawGlyphList(const dd::DrawVertex* glyphs, int count, dd::GlyphTextureHandle glyphTex) override;

    private:
        rhi::RenderDevice* m_RenderDevice {nullptr};
        rhi::PixelFormat   m_ColorFormat {rhi::PixelFormat::eUndefined};

        glm::mat4                  m_ViewProjectionMatrix {1.0f};
        std::optional<rhi::Rect2D> m_OverrideArea;
        rhi::Texture*              m_DepthTexture {nullptr};
        rhi::PixelFormat           m_DepthTestFormat {rhi::PixelFormat::eUndefined};
        bool                       m_NeedsPipelineRebuild {true};

        rhi::ShaderLibraryRuntime m_ShaderLibrary;
        rhi::GraphicsPipeline m_LineGraphicsPipeline;
        rhi::VertexBuffer     m_VertexBuffer;
        rhi::CommandBuffer*   m_CurrentCommandBuffer {nullptr};
    };
} // namespace vultra
