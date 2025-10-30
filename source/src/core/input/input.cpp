#include "vultra/core/input/input.hpp"

namespace vultra
{
    bool Input::getKey(KeyCode key) { return s_KeyStates[static_cast<int>(key)].pressed; }

    bool Input::getKeyDown(KeyCode key) { return s_KeyStates[static_cast<int>(key)].down; }

    bool Input::getKeyUp(KeyCode key) { return s_KeyStates[static_cast<int>(key)].up; }

    bool Input::getKeyRepeat(KeyCode key) { return s_KeyStates[static_cast<int>(key)].repeat; }

    bool Input::getMouseButton(MouseCode button) { return s_MouseButtonStates[static_cast<int>(button)].pressed; }

    bool Input::getMouseButtonDown(MouseCode button)
    {
        return s_MouseButtonStates[static_cast<int>(button)].clicks > 0;
    }

    bool Input::getMouseButtonUp(MouseCode button)
    {
        auto& state = s_MouseButtonStates[static_cast<int>(button)];
        return (!state.pressed && state.clicks > 0);
    }

    int Input::getMouseButtonClicks(MouseCode button) { return s_MouseButtonStates[static_cast<int>(button)].clicks; }

    void Input::setKeyState(int key, InputAction action)
    {
        auto& state = s_KeyStates[key];
        switch (action)
        {
            case InputAction::ePress:
                state.pressed = true;
                state.down    = true;
                break;
            case InputAction::eRelease:
                state.pressed = false;
                state.up      = true;
                break;
            case InputAction::eRepeat:
                state.repeat = true;
                break;
        }
    }

    void Input::setMouseButtonState(int button, MouseButtonState newState)
    {
        auto& state   = s_MouseButtonStates[button];
        state.pressed = newState.pressed;
        state.clicks  = newState.clicks;
    }

    void Input::clearStates()
    {
        for (auto& [key, state] : s_KeyStates)
        {
            state.down   = false;
            state.up     = false;
            state.repeat = false;
        }
        for (auto& [button, state] : s_MouseButtonStates)
        {
            state.clicks = 0;
        }
        s_MousePositionDelta = glm::vec2(0.0f);
        s_MouseScrollDelta   = glm::vec2(0.0f);
    }
} // namespace vultra