#pragma once

#include "vultra/function/app/imgui_app.hpp"
#include "vultra/function/openxr/xr_headset.hpp"

namespace vultra
{
    struct XRConfig
    {
        ImGuiConfig              imguiConfig {};
        std::optional<glm::vec4> clearColor {std::nullopt};
    };

    class XRApp : public ImGuiApp
    {
    public:
        XRApp(std::span<char*> args, const AppConfig& appConfig, const XRConfig& xrConfig = {});
        ~XRApp() override = default;

        void run() override;

    protected:
        virtual void onXrRender(rhi::CommandBuffer&, openxr::XRHeadset::StereoRenderTargetView&, const fsec dt) {}
        virtual void onNormalRender(rhi::CommandBuffer&, const rhi::RenderTargetView, const fsec dt) {}

    private:
        void onRender(rhi::CommandBuffer&, const rhi::RenderTargetView, const fsec dt) override;

    protected:
        openxr::XRHeadset m_Headset;
    };
} // namespace vultra