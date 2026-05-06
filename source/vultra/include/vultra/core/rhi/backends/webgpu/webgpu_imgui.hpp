#pragma once

#include "vultra/core/rhi/interfaces/iimgui.hpp"

namespace vultra::rhi
{
    class RenderDevice;

    class WebGPUImGui final : public IImGui
    {
    public:
        explicit WebGPUImGui(const RenderDevice& renderDevice);
        ~WebGPUImGui() override;

        void init(const os::Window& window,
                  const RenderDevice& renderDevice,
                  const Swapchain& swapchain,
                  bool enableMultiviewport,
                  bool enableDocking) override;
        void shutdown(const std::string& writableRoot, const char* imguiIniFile) override;

        void beginFrame(const os::Window& window) override;
        void render(CommandBuffer& cb) override;
        void postRender() override;
        void processEvent(const os::GeneralWindowEvent& event) override;

        std::uintptr_t addTexture(const Texture& texture, Sampler sampler) override;
        void           removeTexture(std::uintptr_t& textureId) override;

    private:
        const os::Window*   m_Window {nullptr};
        const RenderDevice& m_RenderDevice;
        bool                m_Initialized {false};
        bool                m_WarnedTexturePath {false};
        bool                m_WarnedViewportUnsupported {false};
        bool                m_HasAppliedImGuiCursorOverride {false};
    };
} // namespace vultra::rhi
