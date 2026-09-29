#include <vultra/core/input/input.hpp>
#include <vultra/platform/glfw/input.hpp>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

namespace vultra::platform
{
    namespace
    {
        KeyCode translateKey(int key)
        {
            switch (key)
            {
                case GLFW_KEY_A:
                    return KeyCode::eA;
                case GLFW_KEY_B:
                    return KeyCode::eB;
                case GLFW_KEY_C:
                    return KeyCode::eC;
                case GLFW_KEY_D:
                    return KeyCode::eD;
                case GLFW_KEY_E:
                    return KeyCode::eE;
                case GLFW_KEY_F:
                    return KeyCode::eF;
                case GLFW_KEY_G:
                    return KeyCode::eG;
                case GLFW_KEY_H:
                    return KeyCode::eH;
                case GLFW_KEY_I:
                    return KeyCode::eI;
                case GLFW_KEY_J:
                    return KeyCode::eJ;
                case GLFW_KEY_K:
                    return KeyCode::eK;
                case GLFW_KEY_L:
                    return KeyCode::eL;
                case GLFW_KEY_M:
                    return KeyCode::eM;
                case GLFW_KEY_N:
                    return KeyCode::eN;
                case GLFW_KEY_O:
                    return KeyCode::eO;
                case GLFW_KEY_P:
                    return KeyCode::eP;
                case GLFW_KEY_Q:
                    return KeyCode::eQ;
                case GLFW_KEY_R:
                    return KeyCode::eR;
                case GLFW_KEY_S:
                    return KeyCode::eS;
                case GLFW_KEY_T:
                    return KeyCode::eT;
                case GLFW_KEY_U:
                    return KeyCode::eU;
                case GLFW_KEY_V:
                    return KeyCode::eV;
                case GLFW_KEY_W:
                    return KeyCode::eW;
                case GLFW_KEY_X:
                    return KeyCode::eX;
                case GLFW_KEY_Y:
                    return KeyCode::eY;
                case GLFW_KEY_Z:
                    return KeyCode::eZ;
                case GLFW_KEY_0:
                    return KeyCode::eNum0;
                case GLFW_KEY_1:
                    return KeyCode::eNum1;
                case GLFW_KEY_2:
                    return KeyCode::eNum2;
                case GLFW_KEY_3:
                    return KeyCode::eNum3;
                case GLFW_KEY_4:
                    return KeyCode::eNum4;
                case GLFW_KEY_5:
                    return KeyCode::eNum5;
                case GLFW_KEY_6:
                    return KeyCode::eNum6;
                case GLFW_KEY_7:
                    return KeyCode::eNum7;
                case GLFW_KEY_8:
                    return KeyCode::eNum8;
                case GLFW_KEY_9:
                    return KeyCode::eNum9;
                case GLFW_KEY_F1:
                    return KeyCode::eF1;
                case GLFW_KEY_F2:
                    return KeyCode::eF2;
                case GLFW_KEY_F3:
                    return KeyCode::eF3;
                case GLFW_KEY_F4:
                    return KeyCode::eF4;
                case GLFW_KEY_F5:
                    return KeyCode::eF5;
                case GLFW_KEY_F6:
                    return KeyCode::eF6;
                case GLFW_KEY_F7:
                    return KeyCode::eF7;
                case GLFW_KEY_F8:
                    return KeyCode::eF8;
                case GLFW_KEY_F9:
                    return KeyCode::eF9;
                case GLFW_KEY_F10:
                    return KeyCode::eF10;
                case GLFW_KEY_F11:
                    return KeyCode::eF11;
                case GLFW_KEY_F12:
                    return KeyCode::eF12;
                case GLFW_KEY_KP_0:
                    return KeyCode::eKP0;
                case GLFW_KEY_KP_1:
                    return KeyCode::eKP1;
                case GLFW_KEY_KP_2:
                    return KeyCode::eKP2;
                case GLFW_KEY_KP_3:
                    return KeyCode::eKP3;
                case GLFW_KEY_KP_4:
                    return KeyCode::eKP4;
                case GLFW_KEY_KP_5:
                    return KeyCode::eKP5;
                case GLFW_KEY_KP_6:
                    return KeyCode::eKP6;
                case GLFW_KEY_KP_7:
                    return KeyCode::eKP7;
                case GLFW_KEY_KP_8:
                    return KeyCode::eKP8;
                case GLFW_KEY_KP_9:
                    return KeyCode::eKP9;
                case GLFW_KEY_ENTER:
                    return KeyCode::eReturn;
                case GLFW_KEY_ESCAPE:
                    return KeyCode::eEscape;
                case GLFW_KEY_BACKSPACE:
                    return KeyCode::eBackspace;
                case GLFW_KEY_TAB:
                    return KeyCode::eTab;
                case GLFW_KEY_SPACE:
                    return KeyCode::eSpace;
                case GLFW_KEY_MINUS:
                    return KeyCode::eMinus;
                case GLFW_KEY_EQUAL:
                    return KeyCode::eEquals;
                case GLFW_KEY_LEFT_BRACKET:
                    return KeyCode::eLeftBracket;
                case GLFW_KEY_RIGHT_BRACKET:
                    return KeyCode::eRightBracket;
                case GLFW_KEY_BACKSLASH:
                    return KeyCode::eBackslash;
                case GLFW_KEY_SEMICOLON:
                    return KeyCode::eSemicolon;
                case GLFW_KEY_APOSTROPHE:
                    return KeyCode::eApostrophe;
                case GLFW_KEY_GRAVE_ACCENT:
                    return KeyCode::eGrave;
                case GLFW_KEY_COMMA:
                    return KeyCode::eComma;
                case GLFW_KEY_PERIOD:
                    return KeyCode::ePeriod;
                case GLFW_KEY_SLASH:
                    return KeyCode::eSlash;
                case GLFW_KEY_CAPS_LOCK:
                    return KeyCode::eCapsLock;
                case GLFW_KEY_PRINT_SCREEN:
                    return KeyCode::ePrintScreen;
                case GLFW_KEY_SCROLL_LOCK:
                    return KeyCode::eScrollLock;
                case GLFW_KEY_PAUSE:
                    return KeyCode::ePause;
                case GLFW_KEY_INSERT:
                    return KeyCode::eInsert;
                case GLFW_KEY_HOME:
                    return KeyCode::eHome;
                case GLFW_KEY_PAGE_UP:
                    return KeyCode::ePageUp;
                case GLFW_KEY_DELETE:
                    return KeyCode::eDelete;
                case GLFW_KEY_END:
                    return KeyCode::eEnd;
                case GLFW_KEY_PAGE_DOWN:
                    return KeyCode::ePageDown;
                case GLFW_KEY_RIGHT:
                    return KeyCode::eRight;
                case GLFW_KEY_LEFT:
                    return KeyCode::eLeft;
                case GLFW_KEY_DOWN:
                    return KeyCode::eDown;
                case GLFW_KEY_UP:
                    return KeyCode::eUp;
                case GLFW_KEY_NUM_LOCK:
                    return KeyCode::eNumLock;
                case GLFW_KEY_KP_DIVIDE:
                    return KeyCode::eKPDivide;
                case GLFW_KEY_KP_MULTIPLY:
                    return KeyCode::eKPMultiply;
                case GLFW_KEY_KP_SUBTRACT:
                    return KeyCode::eKPMinus;
                case GLFW_KEY_KP_ADD:
                    return KeyCode::eKPPlus;
                case GLFW_KEY_KP_ENTER:
                    return KeyCode::eKPEnter;
                case GLFW_KEY_KP_DECIMAL:
                    return KeyCode::eKPPeriod;
                case GLFW_KEY_LEFT_CONTROL:
                    return KeyCode::eLCtrl;
                case GLFW_KEY_LEFT_SHIFT:
                    return KeyCode::eLShift;
                case GLFW_KEY_LEFT_ALT:
                    return KeyCode::eLAlt;
                case GLFW_KEY_LEFT_SUPER:
                    return KeyCode::eLGUI;
                case GLFW_KEY_RIGHT_CONTROL:
                    return KeyCode::eRCtrl;
                case GLFW_KEY_RIGHT_SHIFT:
                    return KeyCode::eRShift;
                case GLFW_KEY_RIGHT_ALT:
                    return KeyCode::eRAlt;
                case GLFW_KEY_RIGHT_SUPER:
                    return KeyCode::eRGUI;
                case GLFW_KEY_MENU:
                    return KeyCode::eMenu;
                default:
                    return KeyCode::eUnknown;
            }
        }

        Input& inputFor(GLFWwindow* window)
        {
            return *static_cast<Input*>(glfwGetWindowUserPointer(window));
        }
    } // namespace

    void installGlfwInput(GLFWwindow* window, Input& input)
    {
        glfwSetWindowUserPointer(window, &input);
        input.setFocused(glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0);
        double x = 0;
        double y = 0;
        glfwGetCursorPos(window, &x, &y);
        input.setMousePosition({float(x), float(y)});
        input.advanceFrame();
        glfwSetKeyCallback(window,
                           [](GLFWwindow* handle, int key, int, int action, int)
                           {
                               InputAction translated = InputAction::eRelease;
                               if (action == GLFW_PRESS)
                               {
                                   translated = InputAction::ePress;
                               }
                               else if (action == GLFW_REPEAT)
                               {
                                   translated = InputAction::eRepeat;
                               }
                               inputFor(handle).setKey(translateKey(key), translated);
                           });
        glfwSetMouseButtonCallback(window,
                                   [](GLFWwindow* handle, int button, int action, int)
                                   {
                                       MouseCode translated = MouseCode::eCount;
                                       switch (button)
                                       {
                                           case GLFW_MOUSE_BUTTON_LEFT:
                                               translated = MouseCode::eLeft;
                                               break;
                                           case GLFW_MOUSE_BUTTON_MIDDLE:
                                               translated = MouseCode::eMiddle;
                                               break;
                                           case GLFW_MOUSE_BUTTON_RIGHT:
                                               translated = MouseCode::eRight;
                                               break;
                                           case GLFW_MOUSE_BUTTON_4:
                                               translated = MouseCode::eX1;
                                               break;
                                           case GLFW_MOUSE_BUTTON_5:
                                               translated = MouseCode::eX2;
                                               break;
                                           default:
                                               break;
                                       }
                                       inputFor(handle).setMouseButton(translated, action == GLFW_PRESS);
                                   });
        glfwSetCursorPosCallback(window,
                                 [](GLFWwindow* handle, double x, double y)
                                 {
                                     inputFor(handle).setMousePosition({float(x), float(y)});
                                 });
        glfwSetScrollCallback(window,
                              [](GLFWwindow* handle, double x, double y)
                              {
                                  inputFor(handle).addMouseScroll({float(x), float(y)});
                              });
        glfwSetWindowFocusCallback(window,
                                   [](GLFWwindow* handle, int focused)
                                   {
                                       inputFor(handle).setFocused(focused != 0);
                                   });
    }
} // namespace vultra::platform
