# Editor Content Asset Creation

## Intent

The Content Browser should create project-facing assets through registered
creators instead of hard-coded menu actions.

This registry is the C++ anchor for future editor extensions. Built-in creators
and Lua/plugin-provided creators should eventually share the same descriptor
shape: menu path, default filename, extension, asset type, and a creation
handler.

## Current Shape

- `ContentAssetRegistry` stores editor asset creators.
- Built-ins register once at Content Browser startup.
- The Content Browser renders `Create` menu items from the registry.
- Created files are written under the selected project asset root and then
  reimported so the asset registry can resolve them.

## Built-In Creators

- `vultra.scene`: creates `.vscn` scene documents.
- `vultra.lua_script`: creates `.lua` gameplay scripts and opens them in the
  Code Editor.

## Future Lua Plugin Direction

A Lua-based editor plugin system should expose a narrow API that registers
descriptors into the same registry. Plugins should not directly mutate Content
Browser UI state. They should provide creation metadata and a safe text or
operation handler that the editor invokes inside project-write guardrails.

Example eventual Lua shape:

```lua
editor.assets.register_creator({
    id = "example.dialogue",
    menu = "Script/Dialogue",
    default_name = "NewDialogue.lua",
    extension = ".lua",
    asset_type = "ScriptLua",
    open_in_code_editor = true,
    create_text = function(name)
        return "-- " .. name .. "\n"
    end,
})
```

## Guardrails

- Creators may write only inside the current project asset root.
- Created source assets must be passed through the asset import/registry path.
- Lua plugins should run through an editor-owned service, not window code.
- Engine asset formats remain project-facing API; prefer additive template
  changes over renaming extensions.
