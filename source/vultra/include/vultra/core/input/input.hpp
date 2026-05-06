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
        static bool getKey(KeyCode key);
        static bool getKeyDown(KeyCode key);
        static bool getKeyUp(KeyCode key);
        static bool getKeyRepeat(KeyCode key);

        static bool getMouseButton(MouseCode button);
        static bool getMouseButtonDown(MouseCode button);
        static bool getMouseButtonUp(MouseCode button);
        static int  getMouseButtonClicks(MouseCode button);

        static glm::vec2 getMousePosition() { return s_MousePosition; }
        static glm::vec2 getMousePositionFlipY() { return s_MousePositionFlipY; }
        static glm::vec2 getMousePositionDelta() { return s_MousePositionDelta; }
        static glm::vec2 getMouseScrollDelta() { return s_MouseScrollDelta; }

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