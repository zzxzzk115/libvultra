#pragma once

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/core/input/input_structs.hpp"
#include "vultra/core/os/window.hpp"
#include <vbase/service/service_registry.hpp>

#include <glm/glm.hpp>

namespace vultra
{
    // Lua namespace `Input` (doc/lua_api_design.md). The script bindings are
    // generated from these VBIND_FN annotations via the IR pipeline (see
    // tools/python/extract_bindings.py); the generated wrappers null-check
    // ScriptContext::inputService before dispatching.
    class VBIND_MODULE(name = Input, service = inputService) IInputService
    {
    public:
        SERVICE_REGISTER(IInputService)

        virtual void handleEvent(const os::GeneralWindowEvent& e) = 0;

        VBIND_FN() virtual bool isKeyHeld(KeyCode key) const     = 0;
        VBIND_FN() virtual bool isKeyPressed(KeyCode key) const  = 0;
        VBIND_FN() virtual bool isKeyReleased(KeyCode key) const = 0;
        VBIND_FN() virtual bool isKeyRepeated(KeyCode key) const = 0;

        VBIND_FN() virtual bool isMouseButtonHeld(MouseCode button) const     = 0;
        VBIND_FN() virtual bool isMouseButtonPressed(MouseCode button) const  = 0;
        VBIND_FN() virtual bool isMouseButtonReleased(MouseCode button) const = 0;
        VBIND_FN() virtual int  mouseButtonClicks(MouseCode button) const     = 0;

        VBIND_FN() virtual glm::vec2 mousePosition() const      = 0;
        VBIND_FN() virtual glm::vec2 mousePositionFlipY() const = 0;
        VBIND_FN() virtual glm::vec2 mousePositionDelta() const = 0;
        VBIND_FN() virtual glm::vec2 mouseScrollDelta() const   = 0;
    };
} // namespace vultra
