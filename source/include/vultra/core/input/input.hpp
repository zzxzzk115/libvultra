#pragma once

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <glm/glm.hpp>

#include <map>

namespace vultra
{
    namespace os
    {
        class Window;
    }

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

    enum class KeyCode
    {
        eUnknown = SDL_SCANCODE_UNKNOWN,

        eA = SDL_SCANCODE_A,
        eB = SDL_SCANCODE_B,
        eC = SDL_SCANCODE_C,
        eD = SDL_SCANCODE_D,
        eE = SDL_SCANCODE_E,
        eF = SDL_SCANCODE_F,
        eG = SDL_SCANCODE_G,
        eH = SDL_SCANCODE_H,
        eI = SDL_SCANCODE_I,
        eJ = SDL_SCANCODE_J,
        eK = SDL_SCANCODE_K,
        eL = SDL_SCANCODE_L,
        eM = SDL_SCANCODE_M,
        eN = SDL_SCANCODE_N,
        eO = SDL_SCANCODE_O,
        eP = SDL_SCANCODE_P,
        eQ = SDL_SCANCODE_Q,
        eR = SDL_SCANCODE_R,
        eS = SDL_SCANCODE_S,
        eT = SDL_SCANCODE_T,
        eU = SDL_SCANCODE_U,
        eV = SDL_SCANCODE_V,
        eW = SDL_SCANCODE_W,
        eX = SDL_SCANCODE_X,
        eY = SDL_SCANCODE_Y,
        eZ = SDL_SCANCODE_Z,

        eNum1 = SDL_SCANCODE_1,
        eNum2 = SDL_SCANCODE_2,
        eNum3 = SDL_SCANCODE_3,
        eNum4 = SDL_SCANCODE_4,
        eNum5 = SDL_SCANCODE_5,
        eNum6 = SDL_SCANCODE_6,
        eNum7 = SDL_SCANCODE_7,
        eNum8 = SDL_SCANCODE_8,
        eNum9 = SDL_SCANCODE_9,
        eNum0 = SDL_SCANCODE_0,

        eReturn    = SDL_SCANCODE_RETURN,
        eEscape    = SDL_SCANCODE_ESCAPE,
        eBackspace = SDL_SCANCODE_BACKSPACE,
        eTab       = SDL_SCANCODE_TAB,
        eSpace     = SDL_SCANCODE_SPACE,

        eMinus        = SDL_SCANCODE_MINUS,
        eEquals       = SDL_SCANCODE_EQUALS,
        eLeftBracket  = SDL_SCANCODE_LEFTBRACKET,
        eRightBracket = SDL_SCANCODE_RIGHTBRACKET,
        eBackslash    = SDL_SCANCODE_BACKSLASH,
        eSemicolon    = SDL_SCANCODE_SEMICOLON,
        eApostrophe   = SDL_SCANCODE_APOSTROPHE,
        eGrave        = SDL_SCANCODE_GRAVE,
        eComma        = SDL_SCANCODE_COMMA,
        ePeriod       = SDL_SCANCODE_PERIOD,
        eSlash        = SDL_SCANCODE_SLASH,

        eCapsLock = SDL_SCANCODE_CAPSLOCK,
        eF1       = SDL_SCANCODE_F1,
        eF2       = SDL_SCANCODE_F2,
        eF3       = SDL_SCANCODE_F3,
        eF4       = SDL_SCANCODE_F4,
        eF5       = SDL_SCANCODE_F5,
        eF6       = SDL_SCANCODE_F6,
        eF7       = SDL_SCANCODE_F7,
        eF8       = SDL_SCANCODE_F8,
        eF9       = SDL_SCANCODE_F9,
        eF10      = SDL_SCANCODE_F10,
        eF11      = SDL_SCANCODE_F11,
        eF12      = SDL_SCANCODE_F12,

        ePrintScreen = SDL_SCANCODE_PRINTSCREEN,
        eScrollLock  = SDL_SCANCODE_SCROLLLOCK,
        ePause       = SDL_SCANCODE_PAUSE,
        eInsert      = SDL_SCANCODE_INSERT,
        eHome        = SDL_SCANCODE_HOME,
        ePageUp      = SDL_SCANCODE_PAGEUP,
        eDelete      = SDL_SCANCODE_DELETE,
        eEnd         = SDL_SCANCODE_END,
        ePageDown    = SDL_SCANCODE_PAGEDOWN,
        eRight       = SDL_SCANCODE_RIGHT,
        eLeft        = SDL_SCANCODE_LEFT,
        eDown        = SDL_SCANCODE_DOWN,
        eUp          = SDL_SCANCODE_UP,

        eNumLock    = SDL_SCANCODE_NUMLOCKCLEAR,
        eKPDivide   = SDL_SCANCODE_KP_DIVIDE,
        eKPMultiply = SDL_SCANCODE_KP_MULTIPLY,
        eKPMinus    = SDL_SCANCODE_KP_MINUS,
        eKPPlus     = SDL_SCANCODE_KP_PLUS,
        eKPEnter    = SDL_SCANCODE_KP_ENTER,
        eKP1        = SDL_SCANCODE_KP_1,
        eKP2        = SDL_SCANCODE_KP_2,
        eKP3        = SDL_SCANCODE_KP_3,
        eKP4        = SDL_SCANCODE_KP_4,
        eKP5        = SDL_SCANCODE_KP_5,
        eKP6        = SDL_SCANCODE_KP_6,
        eKP7        = SDL_SCANCODE_KP_7,
        eKP8        = SDL_SCANCODE_KP_8,
        eKP9        = SDL_SCANCODE_KP_9,
        eKP0        = SDL_SCANCODE_KP_0,
        eKPPeriod   = SDL_SCANCODE_KP_PERIOD,

        eLCtrl  = SDL_SCANCODE_LCTRL,
        eLShift = SDL_SCANCODE_LSHIFT,
        eLAlt   = SDL_SCANCODE_LALT,
        eLGUI   = SDL_SCANCODE_LGUI,
        eRCtrl  = SDL_SCANCODE_RCTRL,
        eRShift = SDL_SCANCODE_RSHIFT,
        eRAlt   = SDL_SCANCODE_RALT,
        eRGUI   = SDL_SCANCODE_RGUI,
        eMenu   = SDL_SCANCODE_MENU
    };

    enum class MouseCode
    {
        eLeft   = SDL_BUTTON_LEFT,
        eMiddle = SDL_BUTTON_MIDDLE,
        eRight  = SDL_BUTTON_RIGHT,
        eX1     = SDL_BUTTON_X1,
        eX2     = SDL_BUTTON_X2
    };

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
        static void setMouseScrollDelta(const glm::vec2& scrollDelta) { s_MouseScrollDelta = scrollDelta; }

        static void clearStates();

        friend class BaseApp;

    private:
        inline static std::map<int, KeyState>         s_KeyStates;
        inline static std::map<int, MouseButtonState> s_MouseButtonStates;

        inline static glm::vec2 s_MousePosition;
        inline static glm::vec2 s_MousePositionFlipY;
        inline static glm::vec2 s_MouseScrollDelta;
    };
} // namespace vultra