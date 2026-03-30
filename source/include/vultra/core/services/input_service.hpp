#pragma once

#include "vultra/core/input/input_structs.hpp"
#include "vultra/core/os/window.hpp"
#include <vbase/service/service_registry.hpp>

#include <glm/glm.hpp>

namespace vultra
{
    class IInputService
    {
    public:
        SERVICE_REGISTER(IInputService)

        virtual void handleEvent(const os::GeneralWindowEvent& e) = 0;

        virtual bool getKey(KeyCode key) const       = 0;
        virtual bool getKeyDown(KeyCode key) const   = 0;
        virtual bool getKeyUp(KeyCode key) const     = 0;
        virtual bool getKeyRepeat(KeyCode key) const = 0;

        virtual bool getMouseButton(MouseCode button) const       = 0;
        virtual bool getMouseButtonDown(MouseCode button) const   = 0;
        virtual bool getMouseButtonUp(MouseCode button) const     = 0;
        virtual int  getMouseButtonClicks(MouseCode button) const = 0;

        virtual glm::vec2 getMousePosition() const      = 0;
        virtual glm::vec2 getMousePositionFlipY() const = 0;
        virtual glm::vec2 getMousePositionDelta() const = 0;
        virtual glm::vec2 getMouseScrollDelta() const   = 0;
    };
} // namespace vultra
