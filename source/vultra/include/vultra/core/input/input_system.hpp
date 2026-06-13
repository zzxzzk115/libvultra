#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/services/input_service.hpp"

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

    private:
        std::map<KeyCode, KeyState>           m_KeyStates;
        std::map<MouseCode, MouseButtonState> m_MouseButtonStates;

        glm::vec2 m_MousePosition;
        glm::vec2 m_MousePositionFlipY;
        glm::vec2 m_MousePositionDelta;
        glm::vec2 m_MouseScrollDelta;
    };
} // namespace vultra
