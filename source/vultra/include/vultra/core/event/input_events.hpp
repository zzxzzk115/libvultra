#pragma once

#include "vultra/core/input/input_structs.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <string>

namespace vultra::event
{
    struct KeyEvent
    {
        KeyCode key {KeyCode::eUnknown};
        bool    repeat {false};
    };

    struct MouseButtonEvent
    {
        MouseCode button {MouseCode::eLeft};
        int       clicks {0};
    };

    struct MouseMotionEvent
    {
        glm::vec2 position {};
        glm::vec2 delta {};
    };

    struct MouseWheelEvent
    {
        glm::vec2 delta {};
    };

    struct GamepadButtonEvent
    {
        GamepadButton button {GamepadButton::eSouth};
        int           which {0}; // device instance id
    };

    struct GamepadAxisEvent
    {
        GamepadAxis axis {GamepadAxis::eLeftX};
        float       value {0.0f}; // normalized: sticks [-1,1], triggers [0,1]
        int         which {0};
    };

    struct GamepadConnectionEvent
    {
        int  which {0};
        bool connected {false};
    };

    struct TouchEvent
    {
        int       id {0};
        glm::vec2 position {0.0f}; // window pixels
        glm::vec2 delta {0.0f};
    };

    struct TextInputEvent
    {
        std::string text; // UTF-8 text committed this event
    };
} // namespace vultra::event
