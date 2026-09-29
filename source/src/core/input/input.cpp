#include <vultra/core/input/input.hpp>

namespace vultra
{
    bool Input::isKeyHeld(KeyCode key) const
    {
        return m_Current.keys[size_t(key)].held;
    }

    bool Input::isKeyPressed(KeyCode key) const
    {
        return m_Current.keys[size_t(key)].pressed;
    }

    bool Input::isKeyReleased(KeyCode key) const
    {
        return m_Current.keys[size_t(key)].released;
    }

    bool Input::isKeyRepeated(KeyCode key) const
    {
        return m_Current.keys[size_t(key)].repeated;
    }

    bool Input::isMouseButtonHeld(MouseCode button) const
    {
        return m_Current.buttons[size_t(button)].held;
    }

    bool Input::isMouseButtonPressed(MouseCode button) const
    {
        return m_Current.buttons[size_t(button)].pressed;
    }

    bool Input::isMouseButtonReleased(MouseCode button) const
    {
        return m_Current.buttons[size_t(button)].released;
    }

    glm::vec2 Input::mousePosition() const
    {
        return m_Current.position;
    }

    glm::vec2 Input::mousePositionDelta() const
    {
        return m_Current.delta;
    }

    glm::vec2 Input::mouseScrollDelta() const
    {
        return m_Current.scroll;
    }

    bool Input::focused() const
    {
        return m_Current.focused;
    }

    void Input::setKey(KeyCode key, InputAction action)
    {
        if (key == KeyCode::eUnknown || key >= KeyCode::eCount || !m_Pending.focused)
        {
            return;
        }
        auto& state = m_Pending.keys[size_t(key)];
        switch (action)
        {
            case InputAction::ePress:
                state.pressed |= !state.held;
                state.held = true;
                break;
            case InputAction::eRelease:
                state.released |= state.held;
                state.held = false;
                break;
            case InputAction::eRepeat:
                state.repeated = true;
                break;
        }
    }

    void Input::setMouseButton(MouseCode button, bool held)
    {
        if (button >= MouseCode::eCount || !m_Pending.focused)
        {
            return;
        }
        auto& state = m_Pending.buttons[size_t(button)];
        state.pressed |= held && !state.held;
        state.released |= !held && state.held;
        state.held = held;
    }

    void Input::setMousePosition(glm::vec2 position)
    {
        if (m_HasPosition && m_Pending.focused)
        {
            m_Pending.delta += position - m_Pending.position;
        }
        m_Pending.position = position;
        m_HasPosition      = m_Pending.focused;
    }

    void Input::addMouseScroll(glm::vec2 delta)
    {
        if (m_Pending.focused)
        {
            m_Pending.scroll += delta;
        }
    }

    void Input::setFocused(bool focused)
    {
        m_Pending.focused = focused;
        m_HasPosition     = false;
        if (!focused)
        {
            const auto release = [](auto& buttons)
            {
                for (auto& button : buttons)
                {
                    button.released |= button.held;
                    button.held     = false;
                    button.pressed  = false;
                    button.repeated = false;
                }
            };
            release(m_Pending.keys);
            release(m_Pending.buttons);
            m_Pending.delta  = {};
            m_Pending.scroll = {};
        }
    }

    void Input::advanceFrame()
    {
        m_Current             = m_Pending;
        const auto clearEdges = [](auto& buttons)
        {
            for (auto& button : buttons)
            {
                button.pressed  = false;
                button.released = false;
                button.repeated = false;
            }
        };
        clearEdges(m_Pending.keys);
        clearEdges(m_Pending.buttons);
        m_Pending.delta  = {};
        m_Pending.scroll = {};
    }
} // namespace vultra
