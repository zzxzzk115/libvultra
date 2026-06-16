#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/input/input_action.hpp"
#include "vultra/core/services/input_service.hpp"

#include <array>
#include <map>

namespace vultra
{
    class InputSystem : public EngineSubsystem, public IInputService
    {
    public:
        ENGINE_SUBSYSTEM(InputSystem)

        bool isKeyHeld(KeyCode key) const override;
        bool isKeyPressed(KeyCode key) const override;
        bool isKeyReleased(KeyCode key) const override;
        bool isKeyRepeated(KeyCode key) const override;

        bool isMouseButtonHeld(MouseCode button) const override;
        bool isMouseButtonPressed(MouseCode button) const override;
        bool isMouseButtonReleased(MouseCode button) const override;
        int  mouseButtonClicks(MouseCode button) const override;

        glm::vec2 mousePosition() const override;
        glm::vec2 mousePositionFlipY() const override;
        glm::vec2 mousePositionDelta() const override;
        glm::vec2 mouseScrollDelta() const override;

        bool  isGamepadConnected() const override;
        bool  isGamepadButtonHeld(GamepadButton button) const override;
        bool  isGamepadButtonPressed(GamepadButton button) const override;
        bool  isGamepadButtonReleased(GamepadButton button) const override;
        float gamepadAxis(GamepadAxis axis) const override;
        void  rumble(float lowFrequency, float highFrequency, int durationMs) override;

        int       touchCount() const override;
        int       touchId(int index) const override;
        glm::vec2 touchPosition(int index) const override;
        glm::vec2 touchDelta(int index) const override;
        bool      isTouchPressed(int index) const override;
        bool      isTouchReleased(int index) const override;

        void        startTextInput() override;
        void        stopTextInput() override;
        std::string textInput() const override;

        bool  hasAction(const std::string& action) const override;
        bool  isActionHeld(const std::string& action) const override;
        bool  isActionPressed(const std::string& action) const override;
        bool  isActionReleased(const std::string& action) const override;
        float actionAxis(const std::string& action) const override;
        void  clearActionEvents(const std::string& action) override;
        void  bindActionKey(const std::string& action, KeyCode key) override;
        void  bindActionMouseButton(const std::string& action, MouseCode button) override;
        void  bindActionGamepadButton(const std::string& action, GamepadButton button) override;
        void  bindActionGamepadAxis(const std::string& action, GamepadAxis axis, float scale) override;

        void attachWindow(os::Window* window) override;
        bool loadActionsFromJson(std::string_view json) override;

        void handleEvent(const os::GeneralWindowEvent& e) override;

    protected:
        bool onInit() override;
        void onShutdown() override;
        void onPostUpdate(fsec) override;

    private:
        void setKeyState(KeyCode key, InputAction action);
        void setMouseButtonState(MouseCode button, MouseButtonState state);

        void setMousePosition(glm::vec2 v);
        void setMousePositionFlipY(glm::vec2 v);
        void setMousePositionDelta(glm::vec2 v);
        void setMouseScrollDelta(glm::vec2 v);

        void clearStates();

        // True if a single bound event is currently active (held). For gamepad-axis
        // events this means |value| exceeds the action's deadzone.
        bool eventActive(const InputActionEvent& event, float deadzone) const;
        // Signed analog value of an event (key/button -> +/-scale when held, axis -> raw*scale).
        float eventValue(const InputActionEvent& event) const;
        const InputActionDef* findAction(const std::string& action) const;
        InputActionDef&       ensureAction(const std::string& action);

    private:
        std::map<KeyCode, KeyState>           m_KeyStates;
        std::map<MouseCode, MouseButtonState> m_MouseButtonStates;

        glm::vec2 m_MousePosition;
        glm::vec2 m_MousePositionFlipY;
        glm::vec2 m_MousePositionDelta;
        glm::vec2 m_MouseScrollDelta;

        // Gamepad state (first connected controller).
        bool                                                            m_GamepadConnected {false};
        std::array<GamepadButtonState, static_cast<size_t>(GamepadButton::eCount)> m_GamepadButtons {};
        std::array<float, static_cast<size_t>(GamepadAxis::eCount)>      m_GamepadAxes {};

        std::vector<TouchPoint> m_Touches;
        std::string             m_TextInput;

        InputActionMap m_Actions;
        os::Window*    m_Window {nullptr};
    };
} // namespace vultra
