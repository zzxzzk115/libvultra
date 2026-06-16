#include "vultra/core/input/input_system.hpp"
#include "vultra/core/base/common_context.hpp"

#include <algorithm>
#include <cmath>

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

            case event::WindowEventType::eGamepadButtonDown:
                if (e.gamepadButton.has_value())
                {
                    auto& s   = m_GamepadButtons[static_cast<size_t>(e.gamepadButton->button)];
                    s.pressed = true;
                    s.down    = true;
                }
                break;

            case event::WindowEventType::eGamepadButtonUp:
                if (e.gamepadButton.has_value())
                {
                    auto& s   = m_GamepadButtons[static_cast<size_t>(e.gamepadButton->button)];
                    s.pressed = false;
                    s.up      = true;
                }
                break;

            case event::WindowEventType::eGamepadAxisMotion:
                if (e.gamepadAxis.has_value())
                {
                    m_GamepadAxes[static_cast<size_t>(e.gamepadAxis->axis)] = e.gamepadAxis->value;
                }
                break;

            case event::WindowEventType::eGamepadConnected:
                m_GamepadConnected = true;
                break;

            case event::WindowEventType::eGamepadDisconnected:
                m_GamepadConnected = false;
                m_GamepadButtons.fill({});
                m_GamepadAxes.fill(0.0f);
                break;

            case event::WindowEventType::eTouchDown:
                if (e.touch.has_value())
                {
                    auto& t    = m_Touches.emplace_back();
                    t.id       = e.touch->id;
                    t.position = e.touch->position;
                    t.delta    = e.touch->delta;
                    t.down     = true;
                }
                break;

            case event::WindowEventType::eTouchMotion:
                if (e.touch.has_value())
                {
                    for (auto& t : m_Touches)
                        if (t.id == e.touch->id)
                        {
                            t.position = e.touch->position;
                            t.delta    = e.touch->delta;
                            break;
                        }
                }
                break;

            case event::WindowEventType::eTouchUp:
                if (e.touch.has_value())
                {
                    for (auto& t : m_Touches)
                        if (t.id == e.touch->id)
                        {
                            t.position = e.touch->position;
                            t.up       = true;
                            break;
                        }
                }
                break;

            case event::WindowEventType::eTextInput:
                if (e.textInput.has_value())
                    m_TextInput += e.textInput->text;
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

        for (auto& s : m_GamepadButtons)
        {
            s.down = false;
            s.up   = false;
        }

        // Drop touches that ended this frame; clear per-frame edge/delta on the rest.
        std::erase_if(m_Touches, [](const TouchPoint& t) { return t.up; });
        for (auto& t : m_Touches)
        {
            t.down  = false;
            t.delta = {};
        }
        m_TextInput.clear();

        m_MousePositionDelta = {};

        m_MouseScrollDelta = {};
    }

    bool InputSystem::isKeyHeld(KeyCode key) const
    {
        auto it = m_KeyStates.find(key);

        return it != m_KeyStates.end() && it->second.pressed;
    }

    bool InputSystem::isKeyPressed(KeyCode key) const
    {
        auto it = m_KeyStates.find(key);

        return it != m_KeyStates.end() && it->second.down;
    }

    bool InputSystem::isKeyReleased(KeyCode key) const
    {
        auto it = m_KeyStates.find(key);

        return it != m_KeyStates.end() && it->second.up;
    }

    bool InputSystem::isKeyRepeated(KeyCode key) const
    {
        auto it = m_KeyStates.find(key);

        return it != m_KeyStates.end() && it->second.repeat;
    }

    bool InputSystem::isMouseButtonHeld(MouseCode button) const
    {
        auto it = m_MouseButtonStates.find(button);

        return it != m_MouseButtonStates.end() && it->second.pressed;
    }

    bool InputSystem::isMouseButtonPressed(MouseCode button) const
    {
        auto it = m_MouseButtonStates.find(button);

        return it != m_MouseButtonStates.end() && it->second.clicks > 0;
    }

    bool InputSystem::isMouseButtonReleased(MouseCode button) const
    {
        auto it = m_MouseButtonStates.find(button);

        return it != m_MouseButtonStates.end() && !it->second.pressed && it->second.clicks > 0;
    }

    int InputSystem::mouseButtonClicks(MouseCode button) const
    {
        auto it = m_MouseButtonStates.find(button);

        return it != m_MouseButtonStates.end() ? it->second.clicks : 0;
    }

    glm::vec2 InputSystem::mousePosition() const { return m_MousePosition; }

    glm::vec2 InputSystem::mousePositionFlipY() const { return m_MousePositionFlipY; }

    glm::vec2 InputSystem::mousePositionDelta() const { return m_MousePositionDelta; }

    glm::vec2 InputSystem::mouseScrollDelta() const { return m_MouseScrollDelta; }

    // --- Gamepad ---

    bool InputSystem::isGamepadConnected() const { return m_GamepadConnected; }

    bool InputSystem::isGamepadButtonHeld(GamepadButton button) const
    {
        const auto i = static_cast<size_t>(button);
        return i < m_GamepadButtons.size() && m_GamepadButtons[i].pressed;
    }

    bool InputSystem::isGamepadButtonPressed(GamepadButton button) const
    {
        const auto i = static_cast<size_t>(button);
        return i < m_GamepadButtons.size() && m_GamepadButtons[i].down;
    }

    bool InputSystem::isGamepadButtonReleased(GamepadButton button) const
    {
        const auto i = static_cast<size_t>(button);
        return i < m_GamepadButtons.size() && m_GamepadButtons[i].up;
    }

    float InputSystem::gamepadAxis(GamepadAxis axis) const
    {
        const auto i = static_cast<size_t>(axis);
        return i < m_GamepadAxes.size() ? m_GamepadAxes[i] : 0.0f;
    }

    void InputSystem::rumble(float lowFrequency, float highFrequency, int durationMs)
    {
        if (m_Window)
            m_Window->setGamepadRumble(std::clamp(lowFrequency, 0.0f, 1.0f),
                                       std::clamp(highFrequency, 0.0f, 1.0f),
                                       static_cast<uint32_t>(std::max(durationMs, 0)));
    }

    void InputSystem::attachWindow(os::Window* window) { m_Window = window; }

    // --- Touch ---

    int InputSystem::touchCount() const { return static_cast<int>(m_Touches.size()); }

    int InputSystem::touchId(int index) const
    {
        return (index >= 0 && index < static_cast<int>(m_Touches.size())) ? m_Touches[static_cast<size_t>(index)].id : 0;
    }

    glm::vec2 InputSystem::touchPosition(int index) const
    {
        return (index >= 0 && index < static_cast<int>(m_Touches.size())) ? m_Touches[static_cast<size_t>(index)].position
                                                                          : glm::vec2 {0.0f};
    }

    glm::vec2 InputSystem::touchDelta(int index) const
    {
        return (index >= 0 && index < static_cast<int>(m_Touches.size())) ? m_Touches[static_cast<size_t>(index)].delta
                                                                          : glm::vec2 {0.0f};
    }

    bool InputSystem::isTouchPressed(int index) const
    {
        return index >= 0 && index < static_cast<int>(m_Touches.size()) && m_Touches[static_cast<size_t>(index)].down;
    }

    bool InputSystem::isTouchReleased(int index) const
    {
        return index >= 0 && index < static_cast<int>(m_Touches.size()) && m_Touches[static_cast<size_t>(index)].up;
    }

    // --- Text input ---

    void InputSystem::startTextInput()
    {
        if (m_Window)
            m_Window->startTextInput();
    }

    void InputSystem::stopTextInput()
    {
        if (m_Window)
            m_Window->stopTextInput();
    }

    std::string InputSystem::textInput() const { return m_TextInput; }

    // --- Action map ---

    bool InputSystem::loadActionsFromJson(std::string_view json)
    {
        std::string error;
        InputActionMap parsed;
        if (!parseInputActionMapJson(json, parsed, &error))
        {
            VULTRA_CORE_WARN("[InputSystem] Failed to parse input action map: {}", error);
            return false;
        }
        m_Actions = std::move(parsed);
        VULTRA_CORE_INFO("[InputSystem] Loaded {} input action(s)", m_Actions.size());
        return true;
    }

    const InputActionDef* InputSystem::findAction(const std::string& action) const
    {
        auto it = m_Actions.find(action);
        return it != m_Actions.end() ? &it->second : nullptr;
    }

    InputActionDef& InputSystem::ensureAction(const std::string& action) { return m_Actions[action]; }

    bool InputSystem::eventActive(const InputActionEvent& event, float deadzone) const
    {
        switch (event.kind)
        {
            case InputActionEvent::Kind::eKey:
                return isKeyHeld(event.key);
            case InputActionEvent::Kind::eMouseButton:
                return isMouseButtonHeld(event.mouseButton);
            case InputActionEvent::Kind::eGamepadButton:
                return isGamepadButtonHeld(event.gamepadButton);
            case InputActionEvent::Kind::eGamepadAxis:
                return std::abs(gamepadAxis(event.gamepadAxis) * event.scale) >= deadzone;
        }
        return false;
    }

    float InputSystem::eventValue(const InputActionEvent& event) const
    {
        switch (event.kind)
        {
            case InputActionEvent::Kind::eKey:
                return isKeyHeld(event.key) ? event.scale : 0.0f;
            case InputActionEvent::Kind::eMouseButton:
                return isMouseButtonHeld(event.mouseButton) ? event.scale : 0.0f;
            case InputActionEvent::Kind::eGamepadButton:
                return isGamepadButtonHeld(event.gamepadButton) ? event.scale : 0.0f;
            case InputActionEvent::Kind::eGamepadAxis:
                return gamepadAxis(event.gamepadAxis) * event.scale;
        }
        return 0.0f;
    }

    bool InputSystem::hasAction(const std::string& action) const { return findAction(action) != nullptr; }

    bool InputSystem::isActionHeld(const std::string& action) const
    {
        const auto* def = findAction(action);
        if (!def)
            return false;
        return std::any_of(def->events.begin(), def->events.end(),
                           [&](const InputActionEvent& e) { return eventActive(e, def->deadzone); });
    }

    bool InputSystem::isActionPressed(const std::string& action) const
    {
        const auto* def = findAction(action);
        if (!def)
            return false;
        for (const auto& e : def->events)
        {
            switch (e.kind)
            {
                case InputActionEvent::Kind::eKey:
                    if (isKeyPressed(e.key))
                        return true;
                    break;
                case InputActionEvent::Kind::eMouseButton:
                    if (isMouseButtonPressed(e.mouseButton))
                        return true;
                    break;
                case InputActionEvent::Kind::eGamepadButton:
                    if (isGamepadButtonPressed(e.gamepadButton))
                        return true;
                    break;
                case InputActionEvent::Kind::eGamepadAxis:
                    break; // axes have no discrete press edge
            }
        }
        return false;
    }

    bool InputSystem::isActionReleased(const std::string& action) const
    {
        const auto* def = findAction(action);
        if (!def)
            return false;
        for (const auto& e : def->events)
        {
            switch (e.kind)
            {
                case InputActionEvent::Kind::eKey:
                    if (isKeyReleased(e.key))
                        return true;
                    break;
                case InputActionEvent::Kind::eMouseButton:
                    if (isMouseButtonReleased(e.mouseButton))
                        return true;
                    break;
                case InputActionEvent::Kind::eGamepadButton:
                    if (isGamepadButtonReleased(e.gamepadButton))
                        return true;
                    break;
                case InputActionEvent::Kind::eGamepadAxis:
                    break;
            }
        }
        return false;
    }

    float InputSystem::actionAxis(const std::string& action) const
    {
        const auto* def = findAction(action);
        if (!def)
            return 0.0f;
        float value = 0.0f;
        for (const auto& e : def->events)
            value += eventValue(e);
        // Apply deadzone to the combined analog value, then clamp.
        if (std::abs(value) < def->deadzone)
            return 0.0f;
        return std::clamp(value, -1.0f, 1.0f);
    }

    void InputSystem::clearActionEvents(const std::string& action) { ensureAction(action).events.clear(); }

    void InputSystem::bindActionKey(const std::string& action, KeyCode key)
    {
        ensureAction(action).events.push_back(
            InputActionEvent {.kind = InputActionEvent::Kind::eKey, .key = key});
    }

    void InputSystem::bindActionMouseButton(const std::string& action, MouseCode button)
    {
        ensureAction(action).events.push_back(
            InputActionEvent {.kind = InputActionEvent::Kind::eMouseButton, .mouseButton = button});
    }

    void InputSystem::bindActionGamepadButton(const std::string& action, GamepadButton button)
    {
        ensureAction(action).events.push_back(
            InputActionEvent {.kind = InputActionEvent::Kind::eGamepadButton, .gamepadButton = button});
    }

    void InputSystem::bindActionGamepadAxis(const std::string& action, GamepadAxis axis, float scale)
    {
        ensureAction(action).events.push_back(
            InputActionEvent {.kind = InputActionEvent::Kind::eGamepadAxis, .gamepadAxis = axis, .scale = scale});
    }

} // namespace vultra
