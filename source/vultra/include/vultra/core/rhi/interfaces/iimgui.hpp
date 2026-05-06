#pragma once

#include "vultra/core/os/window.hpp"

#include <cstdint>
#include <string>

namespace vultra
{
    namespace os
    {
        class Window;
    }

    namespace rhi
    {
        class CommandBuffer;
        class RenderDevice;
        class Swapchain;
        class Texture;

        class IImGui
        {
        public:
            virtual ~IImGui() = default;

            virtual void init(const os::Window& window,
                              const RenderDevice& renderDevice,
                              const Swapchain& swapchain,
                              bool enableMultiviewport,
                              bool enableDocking) = 0;
            virtual void shutdown(const std::string& writableRoot, const char* imguiIniFile) = 0;

            virtual void beginFrame(const os::Window& window) = 0;
            virtual void render(CommandBuffer& cb)                      = 0;
            virtual void postRender()                                   = 0;
            virtual void processEvent(const os::GeneralWindowEvent& event) = 0;

            virtual std::uintptr_t addTexture(const Texture& texture) = 0;
            virtual void           removeTexture(std::uintptr_t& textureId) = 0;
        };
    } // namespace rhi
} // namespace vultra
