---
name: vultra-component-workflow
description: Use when adding, updating, wiring, reviewing, or debugging libvultra ECS/world components, scene .vscn serialization, EnTT reflection metadata, Inspector/Add Component UI, hierarchy icons, camera cooking, render-world cooking, or component-driven renderer behavior.
---

# Vultra Component Workflow

## Read First

- Start from existing component patterns in `source/vultra/include/vultra/function/world/components`.
- Keep component fields simple and serialization-friendly: `bool`, integers, `float`, `std::string`, `CoreUUID`, and supported `glm` vector/quaternion types.
- Treat `.vscn` field names as stable API. Prefer additive fields with safe defaults over renames.
- Do not touch unrelated generated assets or scene files just to prove serialization.

## Checklist

1. Define the component header.
   - Add `source/vultra/include/vultra/function/world/components/<name>_component.hpp`.
   - Use numeric enums for serialized choices when that matches nearby components.
   - Use `CoreUUID` for asset references.
2. Register scene reflection.
   - Include the header in `source/vultra/src/function/scene/scene_reflection.cpp`.
   - Add `entt::meta_factory<Component>().type("ComponentName"_hs).data<&...>("field"_hs)...`.
3. Register scene serialization.
   - Include the header in `source/vultra/src/function/scene/scene_system.cpp`.
   - Add `m_ComponentRegistry.registerComponent<Component>("ComponentName", {"field", ...});`.
   - Ensure `SceneSystem::parseValueToAny` and `any_to_text` support every field type.
4. Add editor UI.
   - Include the component in `inspector_window.cpp`.
   - Add display names, `metaFieldNameFromId`, specialized combos, or asset picker handling when needed.
   - Add it to `addableComponents()` unless it is internal-only.
   - Draw the reflected component in `drawEntityInspector`.
   - Add a hierarchy icon in `scene_hierarchy_window.cpp` when it helps scanning.
5. Cook runtime data when the renderer needs it.
   - For cameras, update `CameraSystem::cameras()` and any editor/manual camera builders.
   - For renderable or scene-global state, update `RenderWorld`, `RenderWorld::clear()`, and `RenderWorldCooker::cook`.
   - Resolve asset references through `IAssetService`; resolve GPU resources through `IGpuResourceService`.
6. Wire renderer behavior only where the component has runtime meaning.
   - Prefer passing cooked data through `RenderWorld` or `RenderCamera`, instead of reading ECS directly in render passes.
   - Keep camera policy on camera fields; keep scene/world state on environment-style components.
7. Update project templates or sample scenes only when defaults should change.
8. Verify.
   - Run `xmake build -y vultra-app`.
   - For editor-visible components, check Add Component, Inspector editing, save/reload `.vscn`, and runtime behavior.

## Common Patterns

- Camera fields: add to `CameraComponent`, scene meta/registry, `CameraSystem`, and editor/manual camera constructors.
- Scene-global fields: create an explicit component like `EnvironmentComponent`, cook into `RenderWorld`, and let render features consume cooked data.
- Asset fields: use `CoreUUID`; add `expectedAssetTypeForField` and readable labels in Inspector for drag-drop and picker support.
- Choice fields: use `uint32_t` and add an Inspector combo in `drawMetaValue`.

## Pitfalls

- Forgetting `metaFieldNameFromId` can make reflected fields unreadable or uneditable.
- Adding a component header without scene meta means `.vscn` properties will not apply.
- Loading assets inside render passes should be avoided; cook asset handles or pointers before render graph build.
- New fields with no defaults can break old scenes; always default safely.
