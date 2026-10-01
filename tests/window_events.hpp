#pragma once

#include <vultra/platform/os/window.hpp>

#if defined(VULTRA_WINDOW_SDL3)
#include <SDL3/SDL.h>
#else
#include <GLFW/glfw3.h>
#endif

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
#if defined(VULTRA_WINDOW_SDL3)
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
#else
            auto* handle = static_cast<GLFWwindow*>(window.handle());
            m_Scroll     = glfwSetScrollCallback(handle, nullptr);
            glfwSetScrollCallback(handle, m_Scroll);
            m_Focus = glfwSetWindowFocusCallback(handle, nullptr);
            m_Key   = glfwSetKeyCallback(handle, nullptr);
            glfwSetKeyCallback(handle, m_Key);
#endif
        }

        ~WindowEvents()
        {
#if defined(VULTRA_WINDOW_SDL3)
            SDL_SetEventFilter(nullptr, nullptr);
#else
            glfwSetWindowFocusCallback(static_cast<GLFWwindow*>(m_Window.handle()), m_Focus);
#endif
        }

        void scroll(float amount)
        {
#if defined(VULTRA_WINDOW_SDL3)
            SDL_Event event {};
            event.type           = SDL_EVENT_MOUSE_WHEEL;
            event.wheel.windowID = m_Id;
            event.wheel.y        = amount;
            push(event);
#else
            m_Scroll(static_cast<GLFWwindow*>(m_Window.handle()), 0, amount);
#endif
        }

        void pressForward()
        {
#if defined(VULTRA_WINDOW_SDL3)
            SDL_Event event {};
            event.type         = SDL_EVENT_KEY_DOWN;
            event.key.windowID = m_Id;
            event.key.scancode = SDL_SCANCODE_W;
            event.key.key      = SDLK_W;
            event.key.down     = true;
            push(event);
#else
            m_Key(static_cast<GLFWwindow*>(m_Window.handle()), GLFW_KEY_W, 0, GLFW_PRESS, 0);
#endif
        }

        void focus(bool focused)
        {
#if defined(VULTRA_WINDOW_SDL3)
            SDL_Event event {};
            event.type            = focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
            event.window.windowID = m_Id;
            // SDL_ADDEVENT bypasses the real-focus filter above, while exercising normal dispatch.
            if (SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0) != 1)
            {
                throw std::runtime_error(SDL_GetError());
            }
#else
            m_Focus(static_cast<GLFWwindow*>(m_Window.handle()), focused ? GLFW_TRUE : GLFW_FALSE);
#endif
        }

    private:
#if defined(VULTRA_WINDOW_SDL3)
        static void push(SDL_Event& event)
        {
            if (!SDL_PushEvent(&event))
            {
                throw std::runtime_error(SDL_GetError());
            }
        }

        SDL_WindowID m_Id = 0;
#else
        GLFWscrollfun      m_Scroll = nullptr;
        GLFWwindowfocusfun m_Focus  = nullptr;
        GLFWkeyfun         m_Key    = nullptr;
#endif
        vultra::Window& m_Window;
    };
} // namespace test
