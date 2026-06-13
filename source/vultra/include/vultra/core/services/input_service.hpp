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

        virtual bool isKeyHeld(KeyCode key) const       = 0;
        virtual bool isKeyPressed(KeyCode key) const   = 0;
        virtual bool isKeyReleased(KeyCode key) const     = 0;
        virtual bool isKeyRepeated(KeyCode key) const = 0;

        virtual bool isMouseButtonHeld(MouseCode button) const       = 0;
        virtual bool isMouseButtonPressed(MouseCode button) const   = 0;
        virtual bool isMouseButtonReleased(MouseCode button) const     = 0;
        virtual int  mouseButtonClicks(MouseCode button) const = 0;

        virtual glm::vec2 mousePosition() const      = 0;
        virtual glm::vec2 mousePositionFlipY() const = 0;
        virtual glm::vec2 mousePositionDelta() const = 0;
        virtual glm::vec2 mouseScrollDelta() const   = 0;
    };
} // namespace vultra
