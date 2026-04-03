#pragma once

#include "vultra/function/rendering/srp/renderer.hpp"
#include "vultra/function/services/imgui_service.hpp"

#include <array>

namespace vultra
{
    class GaussianSplatFeature;

    class UniversalRenderer final : public FeatureRenderer
    {
    public:
        std::string_view name() const override { return "universal"; }

        void init() override;

        virtual void onImGui() override;

    private:
        GaussianSplatFeature*                   m_GaussianSplatFeature {nullptr};
        std::array<IImGuiService::TextureID, 2> m_XRMirrorTextureIds {0, 0};
        std::array<const rhi::Texture*, 2>      m_XRMirrorTextures {nullptr, nullptr};
    };
} // namespace vultra
