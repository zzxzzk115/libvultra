#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace vultra
{
    // Framework codes, independent of GLFW keys and SDL scancodes.
    enum class KeyCode : uint16_t
    {
        eUnknown,
        eA,
        eB,
        eC,
        eD,
        eE,
        eF,
        eG,
        eH,
        eI,
        eJ,
        eK,
        eL,
        eM,
        eN,
        eO,
        eP,
        eQ,
        eR,
        eS,
        eT,
        eU,
        eV,
        eW,
        eX,
        eY,
        eZ,
        eNum0,
        eNum1,
        eNum2,
        eNum3,
        eNum4,
        eNum5,
        eNum6,
        eNum7,
        eNum8,
        eNum9,
        eReturn,
        eEscape,
        eBackspace,
        eTab,
        eSpace,
        eMinus,
        eEquals,
        eLeftBracket,
        eRightBracket,
        eBackslash,
        eSemicolon,
        eApostrophe,
        eGrave,
        eComma,
        ePeriod,
        eSlash,
        eCapsLock,
        eF1,
        eF2,
        eF3,
        eF4,
        eF5,
        eF6,
        eF7,
        eF8,
        eF9,
        eF10,
        eF11,
        eF12,
        ePrintScreen,
        eScrollLock,
        ePause,
        eInsert,
        eHome,
        ePageUp,
        eDelete,
        eEnd,
        ePageDown,
        eRight,
        eLeft,
        eDown,
        eUp,
        eNumLock,
        eKPDivide,
        eKPMultiply,
        eKPMinus,
        eKPPlus,
        eKPEnter,
        eKP0,
        eKP1,
        eKP2,
        eKP3,
        eKP4,
        eKP5,
        eKP6,
        eKP7,
        eKP8,
        eKP9,
        eKPPeriod,
        eLCtrl,
        eLShift,
        eLAlt,
        eLGUI,
        eRCtrl,
        eRShift,
        eRAlt,
        eRGUI,
        eMenu,
        eCount
    };

    enum class MouseCode : uint8_t
    {
        eLeft,
        eMiddle,
        eRight,
        eX1,
        eX2,
        eCount
    };

    enum class InputAction
    {
        eRelease,
        ePress,
        eRepeat
    };

    // UI ownership is separate from raw window input; capture never erases held/release state.
    struct InputCapture
    {
        bool mouse    = false;
        bool keyboard = false;
    };

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

        // Backend event sink. Accumulate even during event waits; publish once after polling.
        void setKey(KeyCode key, InputAction action);
        void setMouseButton(MouseCode button, bool held);
        void setMousePosition(glm::vec2 position);
        void addMouseScroll(glm::vec2 delta);
        void addText(std::string_view utf8);
        void setFocused(bool focused);
        void advanceFrame();

    private:
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

        State m_Current;
        State m_Pending;
        bool  m_HasPosition = false;
    };
} // namespace vultra
