#pragma once

#include <vultra/core/input/input_codes.hpp>

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vultra
{
    // UI ownership is separate from raw window input; capture never erases held/release state.
    struct InputCapture
    {
        bool mouse    = false;
        bool keyboard = false;
    };

    struct KeyInput
    {
        KeyCode     key;
        InputAction action;
        uint32_t    modifiers = 0;
    };

    struct MouseButtonInput
    {
        MouseCode button;
        bool      held;
        glm::vec2 position {};
        uint32_t  modifiers = 0;
    };

    struct MouseMotionInput
    {
        glm::vec2 position;
        glm::vec2 delta {};
        uint32_t  modifiers = 0;
    };

    struct MouseScrollInput
    {
        glm::vec2 delta;
        glm::vec2 position {};
        uint32_t  modifiers = 0;
    };

    struct TextInput
    {
        std::string text;
    };

    struct FocusInput
    {
        bool focused;
    };

    struct InputEvent
    {
        std::variant<KeyInput, MouseButtonInput, MouseMotionInput, MouseScrollInput, TextInput, FocusInput> value;
        // Routing ownership does not change the raw Input state, especially held/release state.
        bool consumed = false;
    };

    // Capture determines routing ownership; it never changes the raw held/release state.
    bool inputCaptured(const InputEvent& event, InputCapture capture);

    class Input
    {
    public:
        bool             isKeyHeld(KeyCode key) const;
        bool             isKeyPressed(KeyCode key) const;
        bool             isKeyReleased(KeyCode key) const;
        bool             isKeyRepeated(KeyCode key) const;
        bool             isMouseButtonHeld(MouseCode button) const;
        bool             isMouseButtonPressed(MouseCode button) const;
        bool             isMouseButtonReleased(MouseCode button) const;
        glm::vec2        mousePosition() const;
        glm::vec2        mousePositionDelta() const;
        glm::vec2        mouseScrollDelta() const;
        bool             focused() const;
        std::string_view textInput() const;
        // Published in backend order; borrowed until the next advanceFrame(). Headless sinks use the same setters.
        std::span<InputEvent>       events();
        std::span<const InputEvent> events() const;

        // Backend event sink. Accumulate even during event waits; publish once after polling.
        void setKey(KeyCode key, InputAction action);
        void setMouseButton(MouseCode button, bool held);
        void setMousePosition(glm::vec2 position);
        void addMouseScroll(glm::vec2 delta);
        void addText(std::string_view utf8);
        void setFocused(bool focused);
        void advanceFrame();

    private:
        uint32_t pendingModifiers() const;

        struct Button
        {
            bool held     = false;
            bool pressed  = false;
            bool released = false;
            bool repeated = false;
        };

        struct State
        {
            std::array<Button, size_t(KeyCode::eCount)>   keys {};
            std::array<Button, size_t(MouseCode::eCount)> buttons {};
            glm::vec2                                     position {0};
            glm::vec2                                     delta {0};
            glm::vec2                                     scroll {0};
            std::string                                   text;
            bool                                          focused = true;
        };

        State                   m_Current;
        State                   m_Pending;
        bool                    m_HasPosition = false;
        std::vector<InputEvent> m_Events;
        std::vector<InputEvent> m_PendingEvents;
    };
} // namespace vultra
