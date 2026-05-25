---
name: vultra-lua-render-pass
description: Use when adding, updating, wiring, reviewing, or debugging Lua-based render graph passes, project render pipeline/pass assets, shader-backed FullscreenShader-style passes, render graph editor reflection, shader library lookup, or runtime graph integration in libvultra.
---

# Vultra Lua Render Pass Workflow

## Read First

- Preserve user-defined renderer keys and pass keys. Do not remap `default`, custom graph names, or asset names unless the asset itself says so.
- Prefer raw shader/pass ids first. Only try compatibility prefixes such as `fullscreen/` as fallback.
- Never repeatedly reload project shader libraries from an ImGui draw loop.
- Runtime graph layout should use emitted raw DOT when available; JSON metadata is for labels, texture lookup, and fallback.

## Checklist

1. Identify the asset model.
   - Render graph JSON: `resources/render/*.vrg.json`.
   - Render pipeline Lua: `.vrp.lua`.
   - Project pass Lua: commonly under `resources/render/passes/*.lua`.
   - Project shader library: `res://shaders/project.vshaderlib.lua`.
2. Add or modify the Lua pass definition.
   - Keep pass `type` and custom keys exactly as authored.
   - Define stable inputs, outputs, params, and shader refs.
   - Use defaults so editor graph nodes are usable immediately.
3. Register editor-visible pass types.
   - In `render_graph_window.cpp`, project Lua pass discovery should register pass definitions without hardcoded renderer key remaps.
   - Call registration at editor state setup, not every frame when avoidable.
4. Handle shader reflection and loading.
   - Resolve library and fragment from the pass definition or params.
   - Try raw fragment id before fallback ids like `fullscreen/<fragment>`.
   - Use `ShaderLibraryRuntime::hasVariant(hash, stage)` before `load()` to avoid noisy missing-blob extraction errors.
   - If a shader has no `[properties]`, reflection should gracefully return no extra params.
5. Wire the runtime renderer path.
   - Declarative/fullscreen runtime loading should follow the same shader id order as editor reflection.
   - Do not parse source `.vshaderlib.lua` as runtime vshlib when an imported runtime asset exists.
6. Integrate runtime graph preview.
   - Use `FrameGraph` emitted DOT for layout.
   - Keep JSON nodes/edges as metadata for labels, versions, debug texture matching, and fallback.
   - Support resource versions such as `vN` and DOT ids like `R16_2` without creating duplicate or orphan display nodes.
   - Texture lookup should handle exact resource keys, transient `resource:<id>`, and DOT resource ids.
7. Keep editor UX consistent.
   - Keep controls dense and tool-like; add buttons or menus where workflows need them.
   - Share utilities for repeated functionality such as texture save/export.
8. Verify.
   - Run `xmake build -y vultra-app`.
   - Open Render Graph editor and Runtime Graph preview.
   - Confirm no repeated shader library reload logs.
   - Confirm no `Failed to extract vshlib blob` for expected missing variants.
   - Confirm DOT layout, labels, `vN` textures, and double-click texture preview still work.

## Files To Check

- `source/vultra_app/src/editor_app/ui/windows/render_graph_window.cpp`
- `source/vultra/src/function/rendering/srp/declarative_renderer.cpp`
- `source/vultra/src/function/rendering/shader/shader_system.cpp`
- `source/vultra/include/vultra/core/rhi/shader_library.hpp`
- `source/vultra/src/core/rhi/shader_library.cpp`
- `resources/render/*.vrg.json`
- `resources/render/passes/*.lua`
- `resources/shaders/**`
- `builtin/shaders/passes/**`

## Pitfalls

- Mapping custom keys breaks project-owned graphs.
- Falling back to JSON graph layout breaks the DOT layout users expect.
- Adding display nodes from both JSON and DOT can create duplicate unconnected nodes.
- Calling shader reload from draw code creates repeated log spam and frame hitches.
- Comparing `ImTextureID` with `nullptr` may fail on backends where it is not a pointer.
