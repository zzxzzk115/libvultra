#pragma once

#include <vultra/platform/os/window.hpp>

#include <SDL3/SDL.h>

#include <stdexcept>

namespace test
{
    // Deliver native backend events without injecting input into the user's desktop.
    class WindowEvents
    {
    public:
        explicit WindowEvents(vultra::Window& window) :
            m_Window(window)
        {
            m_Id = SDL_GetWindowID(static_cast<SDL_Window*>(window.handle()));
            SDL_SetEventFilter(
                [](void* user, SDL_Event* event)
                {
                    const auto& self = *static_cast<WindowEvents*>(user);
                    // Other test processes can take desktop focus. Script it for this fixture.
                    return (event->type != SDL_EVENT_WINDOW_FOCUS_GAINED &&
                            event->type != SDL_EVENT_WINDOW_FOCUS_LOST) ||
                           event->window.windowID != self.m_Id;
                },
                this);
        }

        ~WindowEvents()
        {
            SDL_SetEventFilter(nullptr, nullptr);
        }

        void scroll(float amount)
        {
            SDL_Event event {};
            event.type           = SDL_EVENT_MOUSE_WHEEL;
            event.wheel.windowID = m_Id;
            event.wheel.y        = amount;
            push(event);
        }

        void pressForward()
        {
            SDL_Event event {};
            event.type         = SDL_EVENT_KEY_DOWN;
            event.key.windowID = m_Id;
            event.key.scancode = SDL_SCANCODE_W;
            event.key.key      = SDLK_W;
            event.key.down     = true;
            push(event);
        }

        void focus(bool focused)
        {
            SDL_Event event {};
            event.type            = focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
            event.window.windowID = m_Id;
            // SDL_ADDEVENT bypasses the real-focus filter above, while exercising normal dispatch.
            if (SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0) != 1)
            {
                throw std::runtime_error(SDL_GetError());
            }
        }

    private:
        static void push(SDL_Event& event)
        {
            if (!SDL_PushEvent(&event))
            {
                throw std::runtime_error(SDL_GetError());
            }
        }

        SDL_WindowID m_Id = 0;
        vultra::Window& m_Window;
    };
} // namespace test
