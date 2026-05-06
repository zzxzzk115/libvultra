#pragma once

#include <cstdint>

namespace vultra
{
    enum class InputAction
    {
        eRelease = 0,
        ePress   = 1,
        eRepeat  = 2
    };

    struct KeyState
    {
        bool pressed = false;
        bool down    = false;
        bool up      = false;
        bool repeat  = false;
    };

    struct MouseButtonState
    {
        bool pressed {false};
        int  clicks {0};
    };

    enum class KeyCode : uint16_t
    {
        eUnknown = 0,

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

        eNum1,
        eNum2,
        eNum3,
        eNum4,
        eNum5,
        eNum6,
        eNum7,
        eNum8,
        eNum9,
        eNum0,

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
        eKP1,
        eKP2,
        eKP3,
        eKP4,
        eKP5,
        eKP6,
        eKP7,
        eKP8,
        eKP9,
        eKP0,
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
    };

    enum class MouseCode : uint8_t
    {
        eLeft   = 1,
        eMiddle = 2,
        eRight  = 3,
        eX1     = 4,
        eX2     = 5
    };
} // namespace vultra
