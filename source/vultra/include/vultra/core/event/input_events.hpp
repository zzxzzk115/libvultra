#pragma once

#include "vultra/core/input/input_structs.hpp"

#include <glm/glm.hpp>

#include <optional>

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
} // namespace vultra::event
