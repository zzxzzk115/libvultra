#include <vultra/platform/input/input.hpp>
#include <vultra/platform/sdl3/input.hpp>

#include <SDL3/SDL_events.h>

namespace vultra::platform
{
    namespace
    {
        KeyCode translateKey(SDL_Scancode key)
        {
            switch (key)
            {
                case SDL_SCANCODE_A:
                    return KeyCode::eA;
                case SDL_SCANCODE_B:
                    return KeyCode::eB;
                case SDL_SCANCODE_C:
                    return KeyCode::eC;
                case SDL_SCANCODE_D:
                    return KeyCode::eD;
                case SDL_SCANCODE_E:
                    return KeyCode::eE;
                case SDL_SCANCODE_F:
                    return KeyCode::eF;
                case SDL_SCANCODE_G:
                    return KeyCode::eG;
                case SDL_SCANCODE_H:
                    return KeyCode::eH;
                case SDL_SCANCODE_I:
                    return KeyCode::eI;
                case SDL_SCANCODE_J:
                    return KeyCode::eJ;
                case SDL_SCANCODE_K:
                    return KeyCode::eK;
                case SDL_SCANCODE_L:
                    return KeyCode::eL;
                case SDL_SCANCODE_M:
                    return KeyCode::eM;
                case SDL_SCANCODE_N:
                    return KeyCode::eN;
                case SDL_SCANCODE_O:
                    return KeyCode::eO;
                case SDL_SCANCODE_P:
                    return KeyCode::eP;
                case SDL_SCANCODE_Q:
                    return KeyCode::eQ;
                case SDL_SCANCODE_R:
                    return KeyCode::eR;
                case SDL_SCANCODE_S:
                    return KeyCode::eS;
                case SDL_SCANCODE_T:
                    return KeyCode::eT;
                case SDL_SCANCODE_U:
                    return KeyCode::eU;
                case SDL_SCANCODE_V:
                    return KeyCode::eV;
                case SDL_SCANCODE_W:
                    return KeyCode::eW;
                case SDL_SCANCODE_X:
                    return KeyCode::eX;
                case SDL_SCANCODE_Y:
                    return KeyCode::eY;
                case SDL_SCANCODE_Z:
                    return KeyCode::eZ;
                case SDL_SCANCODE_0:
                    return KeyCode::eNum0;
                case SDL_SCANCODE_1:
                    return KeyCode::eNum1;
                case SDL_SCANCODE_2:
                    return KeyCode::eNum2;
                case SDL_SCANCODE_3:
                    return KeyCode::eNum3;
                case SDL_SCANCODE_4:
                    return KeyCode::eNum4;
                case SDL_SCANCODE_5:
                    return KeyCode::eNum5;
                case SDL_SCANCODE_6:
                    return KeyCode::eNum6;
                case SDL_SCANCODE_7:
                    return KeyCode::eNum7;
                case SDL_SCANCODE_8:
                    return KeyCode::eNum8;
                case SDL_SCANCODE_9:
                    return KeyCode::eNum9;
                case SDL_SCANCODE_F1:
                    return KeyCode::eF1;
                case SDL_SCANCODE_F2:
                    return KeyCode::eF2;
                case SDL_SCANCODE_F3:
                    return KeyCode::eF3;
                case SDL_SCANCODE_F4:
                    return KeyCode::eF4;
                case SDL_SCANCODE_F5:
                    return KeyCode::eF5;
                case SDL_SCANCODE_F6:
                    return KeyCode::eF6;
                case SDL_SCANCODE_F7:
                    return KeyCode::eF7;
                case SDL_SCANCODE_F8:
                    return KeyCode::eF8;
                case SDL_SCANCODE_F9:
                    return KeyCode::eF9;
                case SDL_SCANCODE_F10:
                    return KeyCode::eF10;
                case SDL_SCANCODE_F11:
                    return KeyCode::eF11;
                case SDL_SCANCODE_F12:
                    return KeyCode::eF12;
                case SDL_SCANCODE_KP_0:
                    return KeyCode::eKP0;
                case SDL_SCANCODE_KP_1:
                    return KeyCode::eKP1;
                case SDL_SCANCODE_KP_2:
                    return KeyCode::eKP2;
                case SDL_SCANCODE_KP_3:
                    return KeyCode::eKP3;
                case SDL_SCANCODE_KP_4:
                    return KeyCode::eKP4;
                case SDL_SCANCODE_KP_5:
                    return KeyCode::eKP5;
                case SDL_SCANCODE_KP_6:
                    return KeyCode::eKP6;
                case SDL_SCANCODE_KP_7:
                    return KeyCode::eKP7;
                case SDL_SCANCODE_KP_8:
                    return KeyCode::eKP8;
                case SDL_SCANCODE_KP_9:
                    return KeyCode::eKP9;
                case SDL_SCANCODE_RETURN:
                    return KeyCode::eReturn;
                case SDL_SCANCODE_ESCAPE:
                    return KeyCode::eEscape;
                case SDL_SCANCODE_BACKSPACE:
                    return KeyCode::eBackspace;
                case SDL_SCANCODE_TAB:
                    return KeyCode::eTab;
                case SDL_SCANCODE_SPACE:
                    return KeyCode::eSpace;
                case SDL_SCANCODE_MINUS:
                    return KeyCode::eMinus;
                case SDL_SCANCODE_EQUALS:
                    return KeyCode::eEquals;
                case SDL_SCANCODE_LEFTBRACKET:
                    return KeyCode::eLeftBracket;
                case SDL_SCANCODE_RIGHTBRACKET:
                    return KeyCode::eRightBracket;
                case SDL_SCANCODE_BACKSLASH:
                    return KeyCode::eBackslash;
                case SDL_SCANCODE_SEMICOLON:
                    return KeyCode::eSemicolon;
                case SDL_SCANCODE_APOSTROPHE:
                    return KeyCode::eApostrophe;
                case SDL_SCANCODE_GRAVE:
                    return KeyCode::eGrave;
                case SDL_SCANCODE_COMMA:
                    return KeyCode::eComma;
                case SDL_SCANCODE_PERIOD:
                    return KeyCode::ePeriod;
                case SDL_SCANCODE_SLASH:
                    return KeyCode::eSlash;
                case SDL_SCANCODE_CAPSLOCK:
                    return KeyCode::eCapsLock;
                case SDL_SCANCODE_PRINTSCREEN:
                    return KeyCode::ePrintScreen;
                case SDL_SCANCODE_SCROLLLOCK:
                    return KeyCode::eScrollLock;
                case SDL_SCANCODE_PAUSE:
                    return KeyCode::ePause;
                case SDL_SCANCODE_INSERT:
                    return KeyCode::eInsert;
                case SDL_SCANCODE_HOME:
                    return KeyCode::eHome;
                case SDL_SCANCODE_PAGEUP:
                    return KeyCode::ePageUp;
                case SDL_SCANCODE_DELETE:
                    return KeyCode::eDelete;
                case SDL_SCANCODE_END:
                    return KeyCode::eEnd;
                case SDL_SCANCODE_PAGEDOWN:
                    return KeyCode::ePageDown;
                case SDL_SCANCODE_RIGHT:
                    return KeyCode::eRight;
                case SDL_SCANCODE_LEFT:
                    return KeyCode::eLeft;
                case SDL_SCANCODE_DOWN:
                    return KeyCode::eDown;
                case SDL_SCANCODE_UP:
                    return KeyCode::eUp;
                case SDL_SCANCODE_NUMLOCKCLEAR:
                    return KeyCode::eNumLock;
                case SDL_SCANCODE_KP_DIVIDE:
                    return KeyCode::eKPDivide;
                case SDL_SCANCODE_KP_MULTIPLY:
                    return KeyCode::eKPMultiply;
                case SDL_SCANCODE_KP_MINUS:
                    return KeyCode::eKPMinus;
                case SDL_SCANCODE_KP_PLUS:
                    return KeyCode::eKPPlus;
                case SDL_SCANCODE_KP_ENTER:
                    return KeyCode::eKPEnter;
                case SDL_SCANCODE_KP_PERIOD:
                    return KeyCode::eKPPeriod;
                case SDL_SCANCODE_LCTRL:
                    return KeyCode::eLCtrl;
                case SDL_SCANCODE_LSHIFT:
                    return KeyCode::eLShift;
                case SDL_SCANCODE_LALT:
                    return KeyCode::eLAlt;
                case SDL_SCANCODE_LGUI:
                    return KeyCode::eLGUI;
                case SDL_SCANCODE_RCTRL:
                    return KeyCode::eRCtrl;
                case SDL_SCANCODE_RSHIFT:
                    return KeyCode::eRShift;
                case SDL_SCANCODE_RALT:
                    return KeyCode::eRAlt;
                case SDL_SCANCODE_RGUI:
                    return KeyCode::eRGUI;
                case SDL_SCANCODE_APPLICATION:
                    return KeyCode::eMenu;
                default:
                    return KeyCode::eUnknown;
            }
        }
    } // namespace

    void processSdlInput(Input& input, const SDL_Event& event)
    {
        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
                input.setKey(translateKey(event.key.scancode),
                             event.key.repeat ? InputAction::eRepeat : InputAction::ePress);
                break;
            case SDL_EVENT_KEY_UP:
                input.setKey(translateKey(event.key.scancode), InputAction::eRelease);
                break;
            case SDL_EVENT_TEXT_INPUT:
                input.addText(event.text.text);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                MouseCode button = MouseCode::eCount;
                switch (event.button.button)
                {
                    case SDL_BUTTON_LEFT:
                        button = MouseCode::eLeft;
                        break;
                    case SDL_BUTTON_MIDDLE:
                        button = MouseCode::eMiddle;
                        break;
                    case SDL_BUTTON_RIGHT:
                        button = MouseCode::eRight;
                        break;
                    case SDL_BUTTON_X1:
                        button = MouseCode::eX1;
                        break;
                    case SDL_BUTTON_X2:
                        button = MouseCode::eX2;
                        break;
                    default:
                        break;
                }
                input.setMouseButton(button, event.button.down);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                input.setMousePosition({event.motion.x, event.motion.y});
                break;
            case SDL_EVENT_MOUSE_WHEEL: {
                const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                input.addMouseScroll({event.wheel.x * direction, event.wheel.y * direction});
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                input.setFocused(true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                input.setFocused(false);
                break;
            default:
                break;
        }
    }
} // namespace vultra::platform
