#pragma once

#include "vultra/core/input/input_structs.hpp"

#include <glm/glm.hpp>

#include <map>

namespace vultra
{
    namespace os
    {
        class Window;
    }

    class Input
    {
    public:
        static bool isKeyHeld(KeyCode key);
        static bool isKeyPressed(KeyCode key);
        static bool isKeyReleased(KeyCode key);
        static bool isKeyRepeated(KeyCode key);

        static bool isMouseButtonHeld(MouseCode button);
        static bool isMouseButtonPressed(MouseCode button);
        static bool isMouseButtonReleased(MouseCode button);
        static int  mouseButtonClicks(MouseCode button);

        static glm::vec2 mousePosition() { return s_MousePosition; }
        static glm::vec2 mousePositionFlipY() { return s_MousePositionFlipY; }
        static glm::vec2 mousePositionDelta() { return s_MousePositionDelta; }
        static glm::vec2 mouseScrollDelta() { return s_MouseScrollDelta; }

    private:
        template<typename T>
        static int toInt(T code)
        {
            return static_cast<int>(code);
        }

        // Update states
        static void setKeyState(int key, InputAction action);
        static void setMouseButtonState(int button, MouseButtonState state);
        static void setMousePosition(const glm::vec2& position) { s_MousePosition = position; }
        static void setMousePositionFlipY(const glm::vec2& positionFlipY) { s_MousePositionFlipY = positionFlipY; }
        static void setMousePositionDelta(const glm::vec2& positionDelta) { s_MousePositionDelta = positionDelta; }
        static void setMouseScrollDelta(const glm::vec2& scrollDelta) { s_MouseScrollDelta = scrollDelta; }

        static void clearStates();

        friend class BaseApp;

    private:
        inline static std::map<int, KeyState>         s_KeyStates;
        inline static std::map<int, MouseButtonState> s_MouseButtonStates;

        inline static glm::vec2 s_MousePosition;
        inline static glm::vec2 s_MousePositionFlipY;
        inline static glm::vec2 s_MousePositionDelta;
        inline static glm::vec2 s_MouseScrollDelta;
    };
} // namespace vultra