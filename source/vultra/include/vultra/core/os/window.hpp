#pragma once

#include "vultra/core/event/window_events.hpp"
#include "vultra/core/rhi/structs/rect2d.hpp"

#include <glm/glm.hpp>
#include <vbase/event/event_bus.hpp>
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#include <vulkan/vulkan.hpp>
#endif

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
#include <optional>
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

            struct CursorImage
            {
                int                  width {0};
                int                  height {0};
                int                  hotX {0};
                int                  hotY {0};
                std::vector<uint8_t> pixels {};

                [[nodiscard]] bool valid() const
                {
                    return width > 0 && height > 0 && hotX >= 0 && hotY >= 0 &&
                           hotX < width && hotY < height &&
                           pixels.size() == static_cast<size_t>(width * height * 4);
                }
            };

            enum class CursorType
            {
                eArrow,
                eOrbit,
                eGrab,
                eLook,
                eZoomIn,
                eZoomOut,
                eTextInput,
                eResizeNS,
                eResizeEW,
                eResizeNESW,
                eResizeNWSE,
                eHand,
                eNotAllowed,
                eCount,
            };

            enum class PlatformType
            {
                eSDL3,
                eGLFW,
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
                Builder& setDecorated(bool decorated);
                Builder& setVisible(bool visible);
                Builder& setPlatform(PlatformType platformType);

                [[nodiscard]] std::shared_ptr<Window> build() const;

            private:
                std::string m_Title;
                Position    m_Position {};
                Extent      m_Extent {};
                bool         m_CursorVisibility {true};
                bool         m_Resizable {true};
                bool         m_Fullscreen {false};
                bool         m_Decorated {true};
                bool         m_Visible {true};
                PlatformType m_PlatformType {PlatformType::eSDL3};
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
            virtual Window& setCustomCursor(const CursorImage& cursorImage) = 0;
            virtual Window& clearCustomCursor()                         = 0;
            virtual Window& setCursorOverride(const CursorImage& cursorImage) = 0;
            virtual Window& clearCursorOverride()                      = 0;
            virtual Window& setCursorVisibility(bool cursorVisibility)   = 0;
            virtual Window& setMouseRelativeMode(bool mouseRelativeMode) = 0;
            virtual Window& setResizable(bool resizable)                 = 0;
            virtual Window& setFullscreen(bool fullscreen)               = 0;
            virtual Window& setDecorated(bool decorated)                 = 0;
            virtual Window& setVisible(bool visible)                     = 0;
            virtual Window& centerOnScreen()                             = 0;

            [[nodiscard]] virtual std::string_view getTitle() const             = 0;
            [[nodiscard]] virtual Extent           getExtent() const            = 0;
            [[nodiscard]] virtual Extent           getFrameBufferExtent() const = 0;
            [[nodiscard]] virtual rhi::Rect2D      getContentArea() const       = 0;
            [[nodiscard]] virtual Position         getPosition() const          = 0;
            [[nodiscard]] virtual CursorType       getCursor() const            = 0;
            [[nodiscard]] virtual bool             hasCustomCursor() const      = 0;
            [[nodiscard]] virtual bool             hasCursorOverride() const    = 0;
            [[nodiscard]] virtual bool             getCursorVisibility() const  = 0;
            [[nodiscard]] virtual bool             getMouseRelativeMode() const = 0;
            [[nodiscard]] virtual bool             isResizable() const          = 0;
            [[nodiscard]] virtual bool             isFullscreen() const         = 0;
            [[nodiscard]] virtual bool             isDecorated() const          = 0;
            [[nodiscard]] virtual bool             isVisible() const            = 0;
            [[nodiscard]] virtual float            getDisplayScale() const      = 0;
            [[nodiscard]] virtual bool             shouldClose() const          = 0;
            [[nodiscard]] virtual bool             isMinimized() const          = 0;
            [[nodiscard]] virtual bool             isMaximized() const          = 0;
            [[nodiscard]] virtual bool             isReady() const              = 0;

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            [[nodiscard]] virtual std::span<const char* const> getRequiredVulkanInstanceExtensions() const      = 0;
            [[nodiscard]] virtual vk::SurfaceKHR               createVulkanSurface(vk::Instance instance) const = 0;
#endif
            [[nodiscard]] virtual WGPUSurface createWebGPUSurface(WGPUInstance instance) const = 0;

            virtual void pollEvents(int timeoutMillis = 0) = 0;

            // Rumble the first connected gamepad. lowFrequency/highFrequency are in [0,1];
            // durationMs is how long the effect plays. No-op when the platform has no
            // gamepad support or none is connected.
            virtual void setGamepadRumble(float /*lowFrequency*/, float /*highFrequency*/, uint32_t /*durationMs*/) {}

            virtual void close()                           = 0;
            virtual void minimize()                        = 0;
            virtual void maximize()                        = 0;
            virtual void restore()                         = 0;

            [[nodiscard]] static std::optional<CursorImage>
                decodeCursorImage(std::span<const uint8_t> encodedBytes, int hotX, int hotY);

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
