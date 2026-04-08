#pragma once

#include "vultra/function/rendering/srp/renderer.hpp"
#include "vultra/function/services/imgui_service.hpp"

#include <array>
#include <vector>

namespace vultra
{
    class GaussianSplatFeature;

    class UniversalRenderer final : public FeatureRenderer
    {
    public:
        enum class RenderPath : uint8_t
        {
            eDefault = 0,
            eCompatibility,
        };

        std::string_view name() const override { return "universal"; }

        void setRenderPath(RenderPath renderPath) { m_RenderPath = renderPath; }
        [[nodiscard]] RenderPath getRenderPath() const { return m_RenderPath; }

        void init() override;

        virtual void onImGui() override;

    private:
        RenderPath                               m_RenderPath {RenderPath::eDefault};
        GaussianSplatFeature*                   m_GaussianSplatFeature {nullptr};
        std::array<IImGuiService::TextureID, 2> m_XRMirrorTextureIds {0, 0};
        std::array<const rhi::Texture*, 2>      m_XRMirrorTextures {nullptr, nullptr};

        std::vector<IImGuiService::TextureID> m_TextureViewerTextureIds;
        std::vector<const rhi::Texture*>      m_TextureViewerRegisteredTextures;
        int                                   m_TextureViewerColumns {4};
    };
} // namespace vultra
