#pragma once

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/core/input/input_structs.hpp"
#include "vultra/core/os/window.hpp"
#include <vbase/service/service_registry.hpp>

#include <glm/glm.hpp>

#include <string>
#include <string_view>

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

        // --- Gamepad (first connected controller) ---
        VBIND_FN() virtual bool  isGamepadConnected() const                       = 0;
        VBIND_FN() virtual bool  isGamepadButtonHeld(GamepadButton button) const  = 0;
        VBIND_FN() virtual bool  isGamepadButtonPressed(GamepadButton button) const  = 0;
        VBIND_FN() virtual bool  isGamepadButtonReleased(GamepadButton button) const = 0;
        VBIND_FN() virtual float gamepadAxis(GamepadAxis axis) const              = 0;
        // Play a rumble effect on the first connected gamepad. Frequencies in [0,1].
        VBIND_FN() virtual void  rumble(float lowFrequency, float highFrequency, int durationMs) = 0;

        // --- Touch (multi-touch; positions in window pixels) ---
        VBIND_FN() virtual int       touchCount() const             = 0;
        VBIND_FN() virtual int       touchId(int index) const       = 0;
        VBIND_FN() virtual glm::vec2 touchPosition(int index) const = 0;
        VBIND_FN() virtual glm::vec2 touchDelta(int index) const    = 0;
        VBIND_FN() virtual bool      isTouchPressed(int index) const  = 0; // began this frame
        VBIND_FN() virtual bool      isTouchReleased(int index) const = 0; // ended this frame

        // --- Text input (enable while a text field is focused) ---
        VBIND_FN() virtual void        startTextInput() = 0;
        VBIND_FN() virtual void        stopTextInput()  = 0;
        VBIND_FN() virtual std::string textInput() const = 0; // UTF-8 typed this frame

        // --- Action map (Godot-style, loaded from res://input.actions.json) ---
        // isActionHeld == currently down; isActionPressed/Released == this-frame edges
        // (consistent with isKeyHeld vs isKeyPressed). actionAxis returns the signed
        // analog value combining the action's events, clamped to [-1, 1].
        VBIND_FN() virtual bool  hasAction(const std::string& action) const         = 0;
        VBIND_FN() virtual bool  isActionHeld(const std::string& action) const      = 0;
        VBIND_FN() virtual bool  isActionPressed(const std::string& action) const   = 0;
        VBIND_FN() virtual bool  isActionReleased(const std::string& action) const  = 0;
        VBIND_FN() virtual float actionAxis(const std::string& action) const        = 0;
        // Runtime rebinding. clearActionEvents wipes an action's bindings; the bind*
        // helpers append a binding (creating the action if absent).
        VBIND_FN() virtual void clearActionEvents(const std::string& action)                          = 0;
        VBIND_FN() virtual void bindActionKey(const std::string& action, KeyCode key)                 = 0;
        VBIND_FN() virtual void bindActionMouseButton(const std::string& action, MouseCode button)    = 0;
        VBIND_FN() virtual void bindActionGamepadButton(const std::string& action, GamepadButton button) = 0;
        VBIND_FN() virtual void bindActionGamepadAxis(const std::string& action, GamepadAxis axis, float scale) = 0;

        // --- Engine/app wiring (not exposed to Lua) ---
        // Connect the platform window so rumble can reach the device.
        virtual void attachWindow(os::Window* window) = 0;
        // Replace the action map from JSON (see parseInputActionMapJson). Returns false on
        // parse failure; the app feeds res://input.actions.json at startup.
        virtual bool loadActionsFromJson(std::string_view json) = 0;
    };
} // namespace vultra
