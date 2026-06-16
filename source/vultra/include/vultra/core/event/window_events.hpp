#pragma once

#include "vultra/core/event/input_events.hpp"

#include <optional>
#include <string>
#include <vector>

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
        eFileDrop,
        eGamepadButtonDown,
        eGamepadButtonUp,
        eGamepadAxisMotion,
        eGamepadConnected,
        eGamepadDisconnected,
    };

    enum class NativeEventSource
    {
        eNone,
        eSDL3,
        eAndroidInput,
    };

    struct FileDropEvent
    {
        std::vector<std::string> paths;
    };

    struct WindowEvent
    {
        WindowEventType                 type {WindowEventType::eUnknown};
        std::optional<KeyEvent>         key;
        std::optional<MouseButtonEvent> mouseButton;
        std::optional<MouseMotionEvent> mouseMotion;
        std::optional<MouseWheelEvent>  mouseWheel;
        std::optional<FileDropEvent>    fileDrop;
        std::optional<GamepadButtonEvent>     gamepadButton;
        std::optional<GamepadAxisEvent>       gamepadAxis;
        std::optional<GamepadConnectionEvent> gamepadConnection;
        const void*                     nativeEvent {nullptr};
        NativeEventSource               nativeEventSource {NativeEventSource::eNone};
    };
} // namespace vultra::event
