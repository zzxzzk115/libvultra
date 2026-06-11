#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/render_upscaler_service.hpp"

#include <vector>

namespace vultra
{
    class RenderUpscalerSystem final : public EngineSubsystem, public IRenderUpscalerService
    {
    public:
        ENGINE_SUBSYSTEM(RenderUpscalerSystem)

        bool onInit() override;
        void onShutdown() override;

        bool registerProvider(IUpscalerProvider& provider) override;
        void unregisterProvider(IUpscalerProvider& provider) override;
        bool setActiveProvider(std::string_view name) override;

        [[nodiscard]] IUpscalerProvider* activeProvider() const override;
        [[nodiscard]] std::vector<std::string> providers() const override;
        [[nodiscard]] UpscalerStatus status() const override;

        [[nodiscard]] UpscalerSettings settings() const override { return m_Settings; }
        void setSettings(const UpscalerSettings& settings) override;
        void setEnabled(bool enabled) override;
        void setMode(UpscalerMode mode) override;

        void onResize(rhi::Extent2D outputExtent) override;
        void beginFrame(const NativeCommandContext& command) override;
        bool evaluate(const UpscalerEvaluateContext& context) override;

    private:
        std::vector<IUpscalerProvider*> m_Providers;
        std::string                     m_ActiveProvider;
        UpscalerSettings                m_Settings;
    };
} // namespace vultra
