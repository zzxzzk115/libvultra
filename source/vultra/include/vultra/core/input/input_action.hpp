#pragma once

#include "vultra/core/input/input_structs.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    // A single physical control bound to an action. Mirrors a Godot "input event":
    // an action fires when any of its bound events is active. For analog reads
    // (InputSystem::actionAxis) a key/button contributes its sign as +/-1 * scale,
    // a gamepad axis contributes its raw value * scale.
    struct InputActionEvent
    {
        enum class Kind : uint8_t
        {
            eKey,
            eMouseButton,
            eGamepadButton,
            eGamepadAxis,
        };

        Kind          kind {Kind::eKey};
        KeyCode       key {KeyCode::eUnknown};
        MouseCode     mouseButton {MouseCode::eLeft};
        GamepadButton gamepadButton {GamepadButton::eSouth};
        GamepadAxis   gamepadAxis {GamepadAxis::eLeftX};
        // Multiplier applied to the analog contribution; also sets the sign of a
        // digital event when used as an axis (e.g. -1 for "move left" on the A key).
        float scale {1.0f};
    };

    struct InputActionDef
    {
        // Below this magnitude an analog event reads as zero (Godot default 0.5).
        float                         deadzone {0.5f};
        std::vector<InputActionEvent> events;
    };

    // action name -> bindings. Loaded from the project config res://input.actions.json
    // (see parseInputActionMapJson) and editable at runtime.
    using InputActionMap = std::unordered_map<std::string, InputActionDef>;

    // Parse a Godot-style input action map from JSON text. Returns false (and leaves
    // outMap untouched) on malformed JSON, writing a reason into outError when given.
    //
    // Schema:
    //   {
    //     "actions": {
    //       "Jump":  { "deadzone": 0.5, "events": [ {"key":"Space"}, {"gamepadButton":"South"} ] },
    //       "MoveX": { "events": [ {"gamepadAxis":"LeftX"}, {"key":"D","scale":1}, {"key":"A","scale":-1} ] }
    //     }
    //   }
    bool parseInputActionMapJson(std::string_view text, InputActionMap& outMap, std::string* outError = nullptr);

    // Serialize an action map back to the res://input.actions.json schema (pretty-printed).
    // Used by the editor's Input Map settings page.
    std::string inputActionMapToJson(const InputActionMap& map);
} // namespace vultra
