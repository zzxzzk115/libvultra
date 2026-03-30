#pragma once

#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"

#include <glm/mat4x4.hpp>

#include <map>
#include <optional>
#include <string>

namespace vultra
{
    class IRenderBackendService;
    class IShaderService;

    class AndroidCompatRenderer final : public Renderer
    {
    public:
        std::string_view name() const override { return "android_compat"; }

        void               init() override;
        void               render(ImmediateRenderContext& ctx) override;
        [[nodiscard]] bool usesFrameGraph() const override { return false; }
        void               onImGui() override;

    private:
        [[nodiscard]] const rhi::GraphicsPipeline* getPipeline(const rhi::PixelFormat colorFormat);

    private:
        rhi::RenderDevice*                                m_RenderDevice {nullptr};
        rhi::SPIRV                                        m_VertexSpirv;
        rhi::SPIRV                                        m_FragmentSpirv;
        bool                                              m_ShaderLoaded {false};
        std::string                                       m_LastError;
        std::map<rhi::PixelFormat, rhi::GraphicsPipeline> m_PipelineCache;
    };
} // namespace vultra
