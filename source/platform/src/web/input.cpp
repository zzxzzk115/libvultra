#include <vultra/platform/web/input.hpp>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vultra::platform
{
    namespace
    {
        void checkEvent(EMSCRIPTEN_RESULT result)
        {
            if (result != EMSCRIPTEN_RESULT_SUCCESS)
            {
                throw std::runtime_error("Register Web input callback failed: " + std::to_string(result));
            }
        }

        KeyCode keyCode(std::string_view code)
        {
            if (code.size() == 4 && code.starts_with("Key") && code[3] >= 'A' && code[3] <= 'Z')
            {
                return KeyCode(uint16_t(KeyCode::eA) + code[3] - 'A');
            }
            if (code.size() == 6 && code.starts_with("Digit") && code[5] >= '0' && code[5] <= '9')
            {
                return KeyCode(uint16_t(KeyCode::eNum0) + code[5] - '0');
            }
            constexpr std::array keys {std::pair {"Enter", KeyCode::eReturn},
                                       std::pair {"Escape", KeyCode::eEscape},
                                       std::pair {"Backspace", KeyCode::eBackspace},
                                       std::pair {"Tab", KeyCode::eTab},
                                       std::pair {"Space", KeyCode::eSpace},
                                       std::pair {"ArrowLeft", KeyCode::eLeft},
                                       std::pair {"ArrowRight", KeyCode::eRight},
                                       std::pair {"ArrowUp", KeyCode::eUp},
                                       std::pair {"ArrowDown", KeyCode::eDown},
                                       std::pair {"ShiftLeft", KeyCode::eLShift},
                                       std::pair {"ShiftRight", KeyCode::eRShift},
                                       std::pair {"ControlLeft", KeyCode::eLCtrl},
                                       std::pair {"ControlRight", KeyCode::eRCtrl},
                                       std::pair {"AltLeft", KeyCode::eLAlt},
                                       std::pair {"AltRight", KeyCode::eRAlt},
                                       std::pair {"MetaLeft", KeyCode::eLGUI},
                                       std::pair {"MetaRight", KeyCode::eRGUI},
                                       std::pair {"Home", KeyCode::eHome},
                                       std::pair {"End", KeyCode::eEnd},
                                       std::pair {"PageUp", KeyCode::ePageUp},
                                       std::pair {"PageDown", KeyCode::ePageDown},
                                       std::pair {"Insert", KeyCode::eInsert},
                                       std::pair {"Delete", KeyCode::eDelete},
                                       std::pair {"F1", KeyCode::eF1},
                                       std::pair {"F2", KeyCode::eF2},
                                       std::pair {"F3", KeyCode::eF3},
                                       std::pair {"F4", KeyCode::eF4},
                                       std::pair {"F5", KeyCode::eF5},
                                       std::pair {"F6", KeyCode::eF6},
                                       std::pair {"F7", KeyCode::eF7},
                                       std::pair {"F8", KeyCode::eF8},
                                       std::pair {"F9", KeyCode::eF9},
                                       std::pair {"F10", KeyCode::eF10},
                                       std::pair {"F11", KeyCode::eF11},
                                       std::pair {"F12", KeyCode::eF12}};
            for (const auto& [name, key] : keys)
            {
                if (code == name)
                {
                    return key;
                }
            }
            return KeyCode::eUnknown;
        }

        MouseCode mouseCode(unsigned short button)
        {
            constexpr std::array buttons {MouseCode::eLeft,
                                          MouseCode::eMiddle,
                                          MouseCode::eRight,
                                          MouseCode::eX1,
                                          MouseCode::eX2};
            return buttons.at(button);
        }
    } // namespace

    struct WebInput::Impl
    {
        explicit Impl(std::string selector) :
            canvas(std::move(selector))
        {
        }

        std::string canvas;
        Input       state;
        bool        lost              = false;
        bool        visibilityChanged = false;
        int         touch             = -1;

        ~Impl()
        {
            const auto* target = canvas.c_str();
            emscripten_set_keydown_callback(target, nullptr, false, nullptr);
            emscripten_set_keyup_callback(target, nullptr, false, nullptr);
            emscripten_set_keypress_callback(target, nullptr, false, nullptr);
            emscripten_set_mousedown_callback(target, nullptr, false, nullptr);
            emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
            emscripten_set_mousemove_callback(target, nullptr, false, nullptr);
            emscripten_set_wheel_callback(target, nullptr, false, nullptr);
            emscripten_set_focus_callback(target, nullptr, false, nullptr);
            emscripten_set_blur_callback(target, nullptr, false, nullptr);
            emscripten_set_touchstart_callback(target, nullptr, false, nullptr);
            emscripten_set_touchmove_callback(target, nullptr, false, nullptr);
            emscripten_set_touchend_callback(target, nullptr, false, nullptr);
            emscripten_set_touchcancel_callback(target, nullptr, false, nullptr);
            emscripten_set_visibilitychange_callback(nullptr, false, nullptr);
            emscripten_set_webglcontextlost_callback(target, nullptr, false, nullptr);
        }

        static EM_BOOL keyboard(int type, const EmscriptenKeyboardEvent* event, void* owner)
        {
            auto& self = *static_cast<Impl*>(owner);
            if (type == EMSCRIPTEN_EVENT_KEYPRESS)
            {
                if (event->charCode >= 32)
                {
                    self.state.addText(event->key);
                }
            }
            else
            {
                auto action = InputAction::eRelease;
                if (type == EMSCRIPTEN_EVENT_KEYDOWN)
                {
                    action = event->repeat ? InputAction::eRepeat : InputAction::ePress;
                }
                self.state.setKey(keyCode(event->code), action);
            }
            return EM_TRUE;
        }

        static EM_BOOL mouse(int type, const EmscriptenMouseEvent* event, void* owner)
        {
            auto& self = *static_cast<Impl*>(owner);
            if (type != EMSCRIPTEN_EVENT_MOUSEUP)
            {
                self.state.setMousePosition({event->targetX, event->targetY});
            }
            if (event->button < 5 && type != EMSCRIPTEN_EVENT_MOUSEMOVE)
            {
                self.state.setMouseButton(mouseCode(event->button), type == EMSCRIPTEN_EVENT_MOUSEDOWN);
            }
            return EM_TRUE;
        }

        static EM_BOOL wheel(int, const EmscriptenWheelEvent* event, void* owner)
        {
            auto& self = *static_cast<Impl*>(owner);
            // Vultra scroll units follow wheel notches; DOM delta can be pixels, lines or pages.
            const double scale = event->deltaMode == DOM_DELTA_PIXEL ? 0.01 : 1.0;
            self.state.addMouseScroll({float(-event->deltaX * scale), float(-event->deltaY * scale)});
            return EM_TRUE;
        }

        static EM_BOOL focus(int type, const EmscriptenFocusEvent*, void* owner)
        {
            auto& self = *static_cast<Impl*>(owner);
            self.state.setFocused(type == EMSCRIPTEN_EVENT_FOCUS);
            return EM_FALSE;
        }

        static EM_BOOL touchEvent(int type, const EmscriptenTouchEvent* event, void* owner)
        {
            auto& self = *static_cast<Impl*>(owner);
            for (int index = 0; index < event->numTouches; ++index)
            {
                const auto& point = event->touches[index];
                if (!point.isChanged)
                {
                    continue;
                }
                if (type == EMSCRIPTEN_EVENT_TOUCHSTART && self.touch == -1)
                {
                    self.touch = point.identifier;
                    self.state.setMouseButton(MouseCode::eLeft, true);
                }
                if (point.identifier != self.touch)
                {
                    continue;
                }
                self.state.setMousePosition({point.targetX, point.targetY});
                if (type == EMSCRIPTEN_EVENT_TOUCHEND || type == EMSCRIPTEN_EVENT_TOUCHCANCEL)
                {
                    self.state.setMouseButton(MouseCode::eLeft, false);
                    self.touch = -1;
                }
            }
            return EM_TRUE;
        }

        static EM_BOOL visibility(int, const EmscriptenVisibilityChangeEvent* event, void* owner)
        {
            auto& self             = *static_cast<Impl*>(owner);
            self.visibilityChanged = true;
            if (event->hidden)
            {
                self.state.setFocused(false);
                self.touch = -1;
            }
            return EM_FALSE;
        }

        static EM_BOOL contextLost(int, const void*, void* owner)
        {
            static_cast<Impl*>(owner)->lost = true;
            return EM_TRUE;
        }

        void attach()
        {
            const auto* target = canvas.c_str();
            checkEvent(emscripten_set_keydown_callback(target, this, false, keyboard));
            checkEvent(emscripten_set_keyup_callback(target, this, false, keyboard));
            checkEvent(emscripten_set_keypress_callback(target, this, false, keyboard));
            checkEvent(emscripten_set_mousedown_callback(target, this, false, mouse));
            checkEvent(emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, mouse));
            checkEvent(emscripten_set_mousemove_callback(target, this, false, mouse));
            checkEvent(emscripten_set_wheel_callback(target, this, false, wheel));
            checkEvent(emscripten_set_focus_callback(target, this, false, focus));
            checkEvent(emscripten_set_blur_callback(target, this, false, focus));
            checkEvent(emscripten_set_touchstart_callback(target, this, false, touchEvent));
            checkEvent(emscripten_set_touchmove_callback(target, this, false, touchEvent));
            checkEvent(emscripten_set_touchend_callback(target, this, false, touchEvent));
            checkEvent(emscripten_set_touchcancel_callback(target, this, false, touchEvent));
            checkEvent(emscripten_set_visibilitychange_callback(this, false, visibility));
            checkEvent(emscripten_set_webglcontextlost_callback(target, this, false, contextLost));
        }
    };

    WebInput::WebInput(std::string canvas) :
        m_Impl(std::make_unique<Impl>(std::move(canvas)))
    {
        m_Impl->attach();
    }

    WebInput::~WebInput() = default;

    Input& WebInput::input()
    {
        return m_Impl->state;
    }

    bool WebInput::contextLost() const
    {
        return m_Impl->lost;
    }

    bool WebInput::consumeVisibilityChange()
    {
        return std::exchange(m_Impl->visibilityChanged, false);
    }
} // namespace vultra::platform
