#pragma once

#include "vultra/function/rendering/srp/renderer.hpp"
#include "vultra/function/services/imgui_service.hpp"

#include <array>
#include <vector>

namespace vultra
{
    class UniversalRenderer final : public FeatureRenderer
    {
    public:
        enum class RenderProfile : uint8_t
        {
            eDefault = 0,
            eCompatibility,
        };

        std::string_view name() const override { return "universal"; }

        void setRenderProfile(RenderProfile renderProfile) { m_RenderProfile = renderProfile; }
        [[nodiscard]] RenderProfile getRenderProfile() const { return m_RenderProfile; }

        void init() override;

        virtual void onImGui() override;

    private:
        RenderProfile                            m_RenderProfile {RenderProfile::eDefault};
        std::array<IImGuiService::TextureID, 2> m_XRMirrorTextureIds {0, 0};
        std::array<const rhi::Texture*, 2>      m_XRMirrorTextures {nullptr, nullptr};

        std::vector<IImGuiService::TextureID> m_TextureViewerTextureIds;
        std::vector<const rhi::Texture*>      m_TextureViewerRegisteredTextures;
        int                                   m_TextureViewerColumns {4};
    };
} // namespace vultra
