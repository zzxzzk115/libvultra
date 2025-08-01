#pragma once

#include "vultra/function/app/base_app.hpp"

namespace vultra
{
    struct ImGuiConfig
    {
        bool enableMultiviewport {true};
        bool enableDocking {true};
    };

    class ImGuiApp : public BaseApp
    {
    public:
        ImGuiApp(std::span<char*>         args,
                 const AppConfig&         appConfig,
                 const ImGuiConfig&       imguiConfig = {.enableMultiviewport = true, .enableDocking = true},
                 std::optional<glm::vec4> clearColor  = {std::nullopt});
        ~ImGuiApp() override;

    protected:
        void onPostUpdate(const fsec dt) override;

        void onPreRender() override;
        void onRender(rhi::CommandBuffer&, const rhi::RenderTargetView, const fsec dt) override;
        void onPostRender() override;

        virtual void onImGui() {}

        virtual void onGeneralWindowEvent(const os::GeneralWindowEvent& event) override;

        void drawGui(rhi::CommandBuffer&, const rhi::RenderTargetView);

    protected:
        std::optional<glm::vec4> m_ClearColor;
    };
} // namespace vultra