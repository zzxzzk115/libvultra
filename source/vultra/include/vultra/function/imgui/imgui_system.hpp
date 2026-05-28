#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/function/services/imgui_service.hpp"

// NOLINTBEGIN
#include <imgui.h>
#include <magic_enum/magic_enum.hpp>
// NOLINTEND

#include <cstdint>
#include <functional>
#include <vector>

namespace vultra
{
    class ImGuiSystem final : public EngineSubsystem, public IImGuiService
    {
    public:
        ENGINE_SUBSYSTEM(ImGuiSystem)

        bool onInit() override;
        void onShutdown() override;

        virtual void      begin() override;
        virtual void      render(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& framebufferInfo) override;
        virtual void      end() override;
        virtual void      postRender() override;
        virtual void      processEvent(const os::GeneralWindowEvent& event) override;
        virtual TextureID addTexture(const rhi::Texture& texture, rhi::Sampler sampler = {}) override;
        virtual void      removeTexture(TextureID& textureID) override;

    private:
        static void initImGui(const rhi::RenderDevice&,
                              const rhi::Swapchain&,
                              const os::Window&,
                              bool                                    enableMultiviewport,
                              bool                                    enableDocking,
                              const std::string&                      writableRoot,
                              const char*                             imguiIniFile,
                              std::function<void(ImGuiDockNodeFlags)> setDockSpace = nullptr);

        static void shutdownImGui(const std::string& writableRoot, const char* imguiIniFile);

        static void setImGuiStyle();

        void collectRetiredTextures(bool force = false);

    private:
        struct RetiredTexture
        {
            std::uintptr_t backendTextureId {0};
            uint64_t       releaseFrame {0};
        };

        static std::function<void(ImGuiDockNodeFlags)> s_SetDockSpace;
        os::Window::Extent m_LastDisplayExtent {0, 0};
        os::Window::Extent m_LastFramebufferExtent {0, 0};
        bool               m_DisplayMetricsInitialized {false};
        uint64_t           m_PostRenderFrame {0};
        std::vector<RetiredTexture> m_RetiredTextures;
    };
} // namespace vultra
