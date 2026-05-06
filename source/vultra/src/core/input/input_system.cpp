#include "vultra/core/input/input_system.hpp"
#include "vultra/core/base/common_context.hpp"

namespace vultra
{
    bool InputSystem::onInit()
    {
        VULTRA_CORE_INFO("[InputSystem] Initializing...");

        VULTRA_CORE_TRACE("[InputSystem] Providing IInputService");
        ctx().services.provide<IInputService>(this);

        VULTRA_CORE_INFO("[InputSystem] Initialized!");

        return true;
    }

    void InputSystem::onShutdown() { VULTRA_CORE_INFO("[InputSystem] Shutting down"); }

    void InputSystem::onPostUpdate(fsec) { clearStates(); }

    void InputSystem::handleEvent(const os::GeneralWindowEvent& e)
    {
        switch (e.type)
        {
            case event::WindowEventType::eKeyDown:
                if (e.key.has_value())
                {
                    setKeyState(e.key->key, e.key->repeat ? InputAction::eRepeat : InputAction::ePress);
                }
                break;

            case event::WindowEventType::eKeyUp:
                if (e.key.has_value())
                {
                    setKeyState(e.key->key, InputAction::eRelease);
                }
                break;

            case event::WindowEventType::eMouseButtonDown:
                if (e.mouseButton.has_value())
                {
                    setMouseButtonState(e.mouseButton->button, {true, e.mouseButton->clicks});
                }
                break;

            case event::WindowEventType::eMouseButtonUp:
                if (e.mouseButton.has_value())
                {
                    setMouseButtonState(e.mouseButton->button, {false, e.mouseButton->clicks});
                }
                break;

            case event::WindowEventType::eMouseMotion:
                if (e.mouseMotion.has_value())
                {
                    setMousePosition(e.mouseMotion->position);
                    setMousePositionDelta(e.mouseMotion->delta);
                }
                break;

            case event::WindowEventType::eMouseWheel:
                if (e.mouseWheel.has_value())
                {
                    setMouseScrollDelta(e.mouseWheel->delta);
                }
                break;

            default:
                break;
        }
    }

    void InputSystem::setKeyState(KeyCode key, InputAction action)
    {
        auto& state = m_KeyStates[key];

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

    void InputSystem::setMouseButtonState(MouseCode button, MouseButtonState s) { m_MouseButtonStates[button] = s; }

    void InputSystem::setMousePosition(glm::vec2 v) { m_MousePosition = v; }

    void InputSystem::setMousePositionFlipY(glm::vec2 v) { m_MousePositionFlipY = v; }

    void InputSystem::setMousePositionDelta(glm::vec2 v) { m_MousePositionDelta = v; }

    void InputSystem::setMouseScrollDelta(glm::vec2 v) { m_MouseScrollDelta = v; }

    void InputSystem::clearStates()
    {
        for (auto& [k, s] : m_KeyStates)
        {
            s.down   = false;
            s.up     = false;
            s.repeat = false;
        }

        for (auto& [b, s] : m_MouseButtonStates)
        {
            s.clicks = 0;
        }

        m_MousePositionDelta = {};

        m_MouseScrollDelta = {};
    }

    bool InputSystem::getKey(KeyCode key) const
    {
        auto it = m_KeyStates.find(key);

        return it != m_KeyStates.end() && it->second.pressed;
    }

    bool InputSystem::getKeyDown(KeyCode key) const
    {
        auto it = m_KeyStates.find(key);

        return it != m_KeyStates.end() && it->second.down;
    }

    bool InputSystem::getKeyUp(KeyCode key) const
    {
        auto it = m_KeyStates.find(key);

        return it != m_KeyStates.end() && it->second.up;
    }

    bool InputSystem::getKeyRepeat(KeyCode key) const
    {
        auto it = m_KeyStates.find(key);

        return it != m_KeyStates.end() && it->second.repeat;
    }

    bool InputSystem::getMouseButton(MouseCode button) const
    {
        auto it = m_MouseButtonStates.find(button);

        return it != m_MouseButtonStates.end() && it->second.pressed;
    }

    bool InputSystem::getMouseButtonDown(MouseCode button) const
    {
        auto it = m_MouseButtonStates.find(button);

        return it != m_MouseButtonStates.end() && it->second.clicks > 0;
    }

    bool InputSystem::getMouseButtonUp(MouseCode button) const
    {
        auto it = m_MouseButtonStates.find(button);

        return it != m_MouseButtonStates.end() && !it->second.pressed && it->second.clicks > 0;
    }

    int InputSystem::getMouseButtonClicks(MouseCode button) const
    {
        auto it = m_MouseButtonStates.find(button);

        return it != m_MouseButtonStates.end() ? it->second.clicks : 0;
    }

    glm::vec2 InputSystem::getMousePosition() const { return m_MousePosition; }

    glm::vec2 InputSystem::getMousePositionFlipY() const { return m_MousePositionFlipY; }

    glm::vec2 InputSystem::getMousePositionDelta() const { return m_MousePositionDelta; }

    glm::vec2 InputSystem::getMouseScrollDelta() const { return m_MouseScrollDelta; }

} // namespace vultra
