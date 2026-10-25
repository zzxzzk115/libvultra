#include <vultra/platform/android/input.hpp>

#include <android/keycodes.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace vultra::platform
{
    namespace
    {
        KeyCode keyCode(int32_t key)
        {
            if (key >= AKEYCODE_A && key <= AKEYCODE_Z)
            {
                return KeyCode(uint16_t(KeyCode::eA) + key - AKEYCODE_A);
            }
            if (key >= AKEYCODE_0 && key <= AKEYCODE_9)
            {
                return KeyCode(uint16_t(KeyCode::eNum0) + key - AKEYCODE_0);
            }
            if (key >= AKEYCODE_F1 && key <= AKEYCODE_F12)
            {
                return KeyCode(uint16_t(KeyCode::eF1) + key - AKEYCODE_F1);
            }
            switch (key)
            {
                case AKEYCODE_ENTER:
                    return KeyCode::eReturn;
                case AKEYCODE_ESCAPE:
                    return KeyCode::eEscape;
                case AKEYCODE_DEL:
                    return KeyCode::eBackspace;
                case AKEYCODE_TAB:
                    return KeyCode::eTab;
                case AKEYCODE_SPACE:
                    return KeyCode::eSpace;
                case AKEYCODE_DPAD_LEFT:
                    return KeyCode::eLeft;
                case AKEYCODE_DPAD_RIGHT:
                    return KeyCode::eRight;
                case AKEYCODE_DPAD_UP:
                    return KeyCode::eUp;
                case AKEYCODE_DPAD_DOWN:
                    return KeyCode::eDown;
                case AKEYCODE_SHIFT_LEFT:
                    return KeyCode::eLShift;
                case AKEYCODE_SHIFT_RIGHT:
                    return KeyCode::eRShift;
                case AKEYCODE_CTRL_LEFT:
                    return KeyCode::eLCtrl;
                case AKEYCODE_CTRL_RIGHT:
                    return KeyCode::eRCtrl;
                case AKEYCODE_ALT_LEFT:
                    return KeyCode::eLAlt;
                case AKEYCODE_ALT_RIGHT:
                    return KeyCode::eRAlt;
                case AKEYCODE_MOVE_HOME:
                    return KeyCode::eHome;
                case AKEYCODE_MOVE_END:
                    return KeyCode::eEnd;
                case AKEYCODE_PAGE_UP:
                    return KeyCode::ePageUp;
                case AKEYCODE_PAGE_DOWN:
                    return KeyCode::ePageDown;
                case AKEYCODE_FORWARD_DEL:
                    return KeyCode::eDelete;
                default:
                    return KeyCode::eUnknown;
            }
        }

        void keyEvent(Input& input, const GameActivityKeyEvent& event)
        {
            auto action = InputAction::eRelease;
            if (event.action == AKEY_EVENT_ACTION_DOWN)
            {
                action = event.repeatCount ? InputAction::eRepeat : InputAction::ePress;
            }
            input.setKey(keyCode(event.keyCode), action);
        }

        void motionEvent(Input& input, const GameActivityMotionEvent& event)
        {
            const auto action = event.action & AMOTION_EVENT_ACTION_MASK;
            if (event.pointerCount)
            {
                input.setMousePosition({GameActivityPointerAxes_getX(&event.pointers[0]),
                                        GameActivityPointerAxes_getY(&event.pointers[0])});
            }
            if (action == AMOTION_EVENT_ACTION_DOWN)
            {
                input.setMouseButton(MouseCode::eLeft, true);
            }
            else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL ||
                     (action == AMOTION_EVENT_ACTION_POINTER_UP &&
                      (event.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) == 0))
            {
                input.setMouseButton(MouseCode::eLeft, false);
            }
            if (action == AMOTION_EVENT_ACTION_BUTTON_PRESS || action == AMOTION_EVENT_ACTION_BUTTON_RELEASE)
            {
                input.setMouseButton(MouseCode::eLeft, event.buttonState & AMOTION_EVENT_BUTTON_PRIMARY);
                input.setMouseButton(MouseCode::eRight, event.buttonState & AMOTION_EVENT_BUTTON_SECONDARY);
                input.setMouseButton(MouseCode::eMiddle, event.buttonState & AMOTION_EVENT_BUTTON_TERTIARY);
            }
            if (action == AMOTION_EVENT_ACTION_SCROLL && event.pointerCount)
            {
                input.addMouseScroll(
                    {GameActivityPointerAxes_getAxisValue(&event.pointers[0], AMOTION_EVENT_AXIS_HSCROLL),
                     GameActivityPointerAxes_getAxisValue(&event.pointers[0], AMOTION_EVENT_AXIS_VSCROLL)});
            }
        }
    } // namespace

    void processAndroidInput(Input& input, android_app& activity)
    {
        auto* buffer = android_app_swap_input_buffers(&activity);
        if (!buffer)
        {
            return;
        }

        struct ClearEvents
        {
            android_input_buffer* buffer;

            ~ClearEvents()
            {
                android_app_clear_key_events(buffer);
                android_app_clear_motion_events(buffer);
            }
        } clear {buffer};

        uint64_t key    = 0;
        uint64_t motion = 0;
        // GameActivity supplies two journals. Merge by event time before publishing Vultra's single journal.
        while (key < buffer->keyEventsCount || motion < buffer->motionEventsCount)
        {
            if (key < buffer->keyEventsCount &&
                (motion == buffer->motionEventsCount ||
                 buffer->keyEvents[key].eventTime <= buffer->motionEvents[motion].eventTime))
            {
                keyEvent(input, buffer->keyEvents[key++]);
            }
            else
            {
                motionEvent(input, buffer->motionEvents[motion++]);
            }
        }
    }
} // namespace vultra::platform
