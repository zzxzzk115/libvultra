#pragma once

#include "vultra/core/event/window_events.hpp"
#include "vultra/core/rhi/structs/rect2d.hpp"

#include <glm/glm.hpp>
#include <vbase/event/event_bus.hpp>
#include <vulkan/vulkan.hpp>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#else
struct WGPUInstanceImpl;
struct WGPUSurfaceImpl;
using WGPUInstance = WGPUInstanceImpl*;
using WGPUSurface  = WGPUSurfaceImpl*;
#endif

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    namespace os
    {
        class Window
        {
        public:
            using Extent   = glm::ivec2;
            using Position = glm::ivec2;

            enum class CursorType
            {
                eArrow,
                eGrab,
            };

            enum class PlatformType
            {
                eSDL3,
                eAndroidNativeWindow,
            };

            enum class DriverType
            {
                eUnknown,
                eX11,
                eWayland,
                eWin32,
                eCocoa,
                eUIKit,
                eAndroid,
            };

            class Builder
            {
            public:
                Builder()                   = default;
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder()                  = default;

                Builder operator=(const Builder&)     = delete;
                Builder operator=(Builder&&) noexcept = delete;

                Builder& setTitle(std::string_view title);
                Builder& setExtent(Extent extent);
                Builder& setPosition(Position position);
                Builder& setCursorVisibility(bool cursorVisibility);
                Builder& setResizable(bool resizable);
                Builder& setFullscreen(bool fullscreen);

                [[nodiscard]] std::shared_ptr<Window> build() const;

            private:
                std::string m_Title;
                Position    m_Position {};
                Extent      m_Extent {};
                bool        m_CursorVisibility {true};
                bool        m_Resizable {true};
                bool        m_Fullscreen {false};
            };

            Window()              = default;
            Window(const Window&) = delete;
            Window(Window&&)      = delete;
            virtual ~Window()     = default;

            Window& operator=(const Window&) = delete;
            Window& operator=(Window&&)      = delete;

            [[nodiscard]] virtual PlatformType platformType() const = 0;
            [[nodiscard]] virtual DriverType   driverType() const   = 0;

            virtual Window& setTitle(std::string_view title)             = 0;
            virtual Window& setExtent(Extent extent)                     = 0;
            virtual Window& setPosition(Position position)               = 0;
            virtual Window& setCursor(CursorType cursor)                 = 0;
            virtual Window& setCursorVisibility(bool cursorVisibility)   = 0;
            virtual Window& setMouseRelativeMode(bool mouseRelativeMode) = 0;
            virtual Window& setResizable(bool resizable)                 = 0;
            virtual Window& setFullscreen(bool fullscreen)               = 0;

            [[nodiscard]] virtual std::string_view getTitle() const             = 0;
            [[nodiscard]] virtual Extent           getExtent() const            = 0;
            [[nodiscard]] virtual Extent           getFrameBufferExtent() const = 0;
            [[nodiscard]] virtual rhi::Rect2D      getContentArea() const       = 0;
            [[nodiscard]] virtual Position         getPosition() const          = 0;
            [[nodiscard]] virtual CursorType       getCursor() const            = 0;
            [[nodiscard]] virtual bool             getCursorVisibility() const  = 0;
            [[nodiscard]] virtual bool             getMouseRelativeMode() const = 0;
            [[nodiscard]] virtual bool             isResizable() const          = 0;
            [[nodiscard]] virtual bool             isFullscreen() const         = 0;
            [[nodiscard]] virtual float            getDisplayScale() const      = 0;
            [[nodiscard]] virtual bool             shouldClose() const          = 0;
            [[nodiscard]] virtual bool             isMinimized() const          = 0;
            [[nodiscard]] virtual bool             isReady() const              = 0;

            [[nodiscard]] virtual std::span<const char* const> getRequiredVulkanInstanceExtensions() const      = 0;
            [[nodiscard]] virtual vk::SurfaceKHR               createVulkanSurface(vk::Instance instance) const = 0;
            [[nodiscard]] virtual WGPUSurface createWebGPUSurface(WGPUInstance instance) const = 0;

            virtual void pollEvents(int timeoutMillis = 0) = 0;
            virtual void close()                           = 0;

            template<typename Event>
            void on(std::function<void(const Event&, Window&)> fn)
            {
                m_Subscriptions.emplace_back(
                    m_EventBus.subscribe<Event>([this, fn = std::move(fn)](const Event& event) { fn(event, *this); }));
            }

            static void shutdownPlatform();

        protected:
            template<typename Event>
            void emitEvent(const Event& event)
            {
                m_EventBus.publish(event);
            }

        private:
            vbase::EventBus                  m_EventBus;
            std::vector<vbase::Subscription> m_Subscriptions;
        };

        using GeneralWindowEvent = event::WindowEvent;
    } // namespace os
} // namespace vultra
