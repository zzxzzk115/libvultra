#pragma once

#include <vultra/core/base/api_annotations.hpp>

#include <cstdint>

namespace vultra
{
    // Framework codes, independent of GLFW keys and SDL scancodes.
    enum class VULTRA_BIND_ENUM KeyCode : uint16_t
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

    enum class VULTRA_BIND_ENUM MouseCode : uint8_t
    {
        eLeft,
        eMiddle,
        eRight,
        eX1,
        eX2,
        eCount
    };

    enum class VULTRA_BIND_ENUM InputAction : uint32_t
    {
        eRelease,
        ePress,
        eRepeat
    };

    enum class VULTRA_BIND_ENUM InputEventKind : uint32_t
    {
        eKey,
        eMouseButton,
        eMouseMotion,
        eMouseScroll,
        eText,
        eFocus
    };

    enum class VULTRA_BIND_ENUM InputModifier : uint32_t
    {
        eNone    = 0,
        eShift   = 1,
        eControl = 2,
        eAlt     = 4,
        eMeta    = 8
    };

} // namespace vultra
