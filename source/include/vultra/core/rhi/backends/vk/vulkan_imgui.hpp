#pragma once

#include "vultra/core/rhi/interfaces/iimgui.hpp"

namespace vultra::rhi
{
    class RenderDevice;

    class VulkanImGui final : public IImGui
    {
    public:
        explicit VulkanImGui(const RenderDevice& renderDevice);
        ~VulkanImGui() override;

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

        std::uintptr_t addTexture(const Texture& texture) override;
        void           removeTexture(std::uintptr_t& textureId) override;

    private:
        const RenderDevice& m_RenderDevice;
        bool                m_Initialized {false};
    };
} // namespace vultra::rhi
