---
name: vultra-component-workflow
description: Use when adding, changing, or reviewing Vultra ECS/world components, including engine runtime components, editor components, UI components, physics/rendering/camera components, scene serialization, Inspector editing, editor/MCP commands, Scene Hierarchy creation menus, runtime cooking, Lua bindings, docs, and verification.
---

# Vultra Component Workflow

Use this checklist whenever a component is added or its public fields change.
Keep the implementation scoped, but do not stop after adding the C++ struct.

## Registration Chain

1. Define the component in the appropriate header under
   `source/vultra/include/vultra/function/world/components/`.
2. Register reflection fields in
   `source/vultra/src/function/scene/scene_reflection.cpp`.
3. Register scene serialization fields in
   `source/vultra/src/function/scene/scene_system.cpp`.
4. Add Inspector support in
   `source/vultra_app/src/editor_app/ui/windows/inspector_window.cpp`:
   display name, Add Component descriptor, default order, presence test, label,
   remove path, and edit branch.
5. Add editor/MCP command support in
   `source/vultra_app/src/editor_app/editor_commands.cpp` when the component
   should be created, updated, removed, or listed by tools.
6. Update Scene Hierarchy creation menus in
   `source/vultra_app/src/editor_app/ui/windows/scene_hierarchy_window.cpp`
   when there is a common entity template for the component.
7. Wire runtime behavior in the owning system. Examples:
   `camera_system.cpp` cooks `CameraComponent`; `render_system.cpp` cooks
   renderable and UI components; `ui_system.cpp` handles UI interaction.
8. Decide Lua parity. If gameplay scripts reasonably need the component, add a
   thin binding in the relevant `script_*_binding.cpp`, update
   `script_types.hpp` when entity refs are needed, and update Lua docs.
9. Update durable docs or AI knowledge only for stable behavior and scripting
   API. Avoid keeping one-off debugging notes as permanent docs.

## Naming

- Use `ComponentNameComponent` for C++ type names.
- Use stable command/component kinds in snake_case.
- Keep aliases for common legacy spellings only in command normalization.
- Use concise Inspector labels matching existing style.

## Verification

- Build with `xmake build -y vultra-app`.
- For editor-visible components, run an MCP smoke when practical:
  list component/entity kinds, create an entity, update component properties,
  select it, capture the editor, and save/reload if serialization changed.
- For runtime components, verify the owning system observes the new fields.
- For Lua-facing changes, verify the binding compiles and document the API in
  `doc/lua_scripting.md` and `ai/knowledge/lua-scripting.md`.
