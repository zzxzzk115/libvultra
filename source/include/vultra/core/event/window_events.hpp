#pragma once

#include "vultra/core/event/input_events.hpp"

#include <optional>

namespace vultra::event
{
    enum class WindowEventType
    {
        eUnknown,
        eQuit,
        eCloseRequested,
        eResized,
        eMoved,
        eKeyDown,
        eKeyUp,
        eMouseButtonDown,
        eMouseButtonUp,
        eMouseMotion,
        eMouseWheel,
    };

    enum class NativeEventSource
    {
        eNone,
        eSDL3,
        eAndroidInput,
    };

    struct WindowEvent
    {
        WindowEventType                 type {WindowEventType::eUnknown};
        std::optional<KeyEvent>         key;
        std::optional<MouseButtonEvent> mouseButton;
        std::optional<MouseMotionEvent> mouseMotion;
        std::optional<MouseWheelEvent>  mouseWheel;
        const void*                     nativeEvent {nullptr};
        NativeEventSource               nativeEventSource {NativeEventSource::eNone};
    };
} // namespace vultra::event
