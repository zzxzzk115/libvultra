#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/imgui_service.hpp"

// NOLINTBEGIN
#include <imgui.h>
#include <magic_enum/magic_enum.hpp>
// NOLINTEND

namespace vultra
{
    class ImGuiSystem final : public EngineSubsystem, public IImGuiService
    {
    public:
        ENGINE_SUBSYSTEM(ImGuiSystem)

        bool onInit() override;
        void onShutdown() override;

        void onRender() override;

        virtual void processEvent(const os::GeneralWindowEvent& event) override;
        virtual void begin() override;
        virtual void render(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& framebufferInfo) override;
        virtual void end() override;
        virtual void postRender() override;

    private:
        static void initImGui(const rhi::RenderDevice&,
                              const rhi::Swapchain&,
                              const os::Window&,
                              bool                                    enableMultiviewport,
                              bool                                    enableDocking,
                              const char*                             imguiIniFile,
                              std::function<void(ImGuiDockNodeFlags)> setDockSpace = nullptr);
        static void shutdownImGui();
        static void setImGuiStyle();

    private:
        static std::function<void(ImGuiDockNodeFlags)> s_SetDockSpace;
    };
} // namespace vultra
