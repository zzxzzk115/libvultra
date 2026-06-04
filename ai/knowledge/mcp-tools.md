# Runtime MCP Tool Manual (for agents)

This is the agent-facing reference for the C++ Runtime MCP exposed by `vultra-app`.
It lists every registered tool, how to call it, and the call semantics. For
host/port/build facts and the fresh-agent decision tree see
`ai/knowledge/harness-config.md`. For starting the editor see `ai/README.md`.

> The authoritative tool set is the dispatch registry in
> `source/vultra_app/src/editor_app/runtime_mcp_tool_registry.cpp` (the `tools/list`
> generator). The exact per-argument JSON Schema for each tool is published by
> `tools/list` at runtime — call it and read `inputSchema` rather than trusting a
> stale copy. The summaries below capture intent and the common arguments; treat
> `tools/list` as ground truth when they disagree, and update this file in the same
> change that alters a tool.

## Connect and Call

- Start editor with MCP forced on:
  `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`
- Endpoint: `POST http://127.0.0.1:8848/mcp` (override port with `--mcp-port`). Host is
  always coerced to `127.0.0.1`. Protocol version `2025-03-26`.
- First smoke sequence: `initialize` → `tools/list` → `tools/call vultra.runtime.status`.
- Request body:
  ```json
  { "jsonrpc": "2.0", "id": 1, "method": "tools/call",
    "params": { "name": "vultra.runtime.status", "arguments": {} } }
  ```
- Success: `result.content[0].text` is a JSON **string** — parse it; tool payloads
  conventionally include `"ok": true`. Tool-level failures come back as
  `result.isError = true` with `{"ok": false, "error": "..."}` in the text.

### Argument casing

Public tool arguments are **snake_case** (`component_kind`, `entity_kind`,
`output_directory`-style intent). Some editor commands tolerate `componentKind`/`kind`
aliases for back-compat, but `tools/list` advertises only the snake_case form — author
new calls in snake_case. (Capture/stream tools use a few camelCase keys such as
`jpegQuality`/`outputFile`; confirm each tool's schema via `tools/list`.)

### Deferred calls and the deadline

- Calls run on the editor main thread; the HTTP worker waits up to a **5s deadline**.
- Some tools defer (re-queue across editor ticks) until their work completes:
  `vultra.sim.step` (waits N frames), `vultra.editor.recording` (start/stop encode),
  `vultra.render.capture_depth` (async readback). If a call exceeds the deadline it
  returns a JSON-RPC error (`code -32000`, "timed out waiting for main thread") — retry
  or reduce the requested work (fewer frames, smaller capture).
- The editor must be ticking (window focused/visible) for calls to execute.

## Tool Catalog

### `vultra.runtime.*` — live runtime/render diagnostics
- `vultra.runtime.status` `{}` — mode, project, scene, renderer keys, play/pause, frame index, backend, xr.
- `vultra.runtime.playback` `{action: play|pause|resume|stop|step}` — drive editor playback.
- `vultra.runtime.profiler` `{}` — frame CPU/GPU ms, draw calls, CPU/GPU scopes.
- `vultra.runtime.framegraph_snapshot` `{}` — enable + return a frame graph snapshot (lists passes).
- `vultra.runtime.frame_resources` `{}` — debug texture metadata for the frame graph.
- `vultra.runtime.scene_context` `{includeRenderGraphSource?}` — open scene, cameras, main camera, renderer config.
- `vultra.runtime.rendergraph_source` `{rendererKey?, uri?, includeSource?}` — render graph source for a key/URI.
- `vultra.runtime.dump_frame_textures` `{outputDirectory, filter?, camera?, renderer?, maxTextures?, captureFrames?, maxPreviewExtent?}` — dump debug textures to PNG.
- `vultra.runtime.reload_pipeline` `{asset?, rendererKey?}` — reload the render graph pipeline.
- `vultra.runtime.capture_frame` `{}` — request a RenderDoc capture.

### `vultra.sim.*` — headless simulation control (deferred where noted)
- `vultra.sim.reset` `{scene?, seed?, play?}` — reset the world, optionally load a scene.
- `vultra.sim.step` `{frames, actions?, includeState?}` — advance N frames, apply actions, return state. **Deferred.**
- `vultra.sim.get_state_batch` `{entities?, limit?, includeInactive?, includeVelocity?}` — read transform/status/physics.
- `vultra.sim.set_state_batch` `{states: [...]}` — write transform/velocity/active/visible per entity.
- `vultra.sim.apply_actions_batch` `{actions: [...]}` — force/impulse/target_velocity/teleport per entity.

### `vultra.render.*` — frame capture / streaming
- `vultra.render.capture_rgb` `{outputFile, camera?, width?, height?}` — capture rendered RGB to PNG.
- `vultra.render.capture_depth` `{outputDirectory, camera?, maxTextures?}` — capture depth-like resources. **Deferred.**
- `vultra.render.stream` `{action: start|stop|status, fps?, jpegQuality?, maxWidth?, maxHeight?}` — MJPEG stream at `/stream/<id>`.

### `vultra.project.*` — project lifecycle
- `vultra.project.info` `{}` — loaded project metadata + generation counters.
- `vultra.project.create_empty` `{projectDir, name?, template?, allowCreate}` — create + open an empty project (requires `allowCreate: true`).

### `vultra.scene.*` — scene/entity/component authoring
- `vultra.scene.new` `{uri?, withDefaults?}` — new scene workspace.
- `vultra.scene.list_entity_kinds` `{}` — entity templates (empty/primitive/camera/light/environment).
- `vultra.scene.list_component_kinds` `{}` — available component kinds.
- `vultra.scene.component_metadata` `{component_kind?}` — field metadata per component kind.
- `vultra.scene.get_component` `{entity, component_kind}` — read a component as JSON.
- `vultra.scene.add_entity` `{entity_kind, primitive?, light_kind?, parent?, name?, position?, rotation?, scale?}` — create entity from template.
- `vultra.scene.remove_entity` `{entity}` — remove entity and children.
- `vultra.scene.add_component` `{entity, component_kind, properties?}` — add a component.
- `vultra.scene.update_component` `{entity, component_kind, properties?}` — update component fields.
- `vultra.scene.remove_component` `{entity, component_kind}` — remove an optional component.
- `vultra.scene.select_entity` `{entity?, clear?}` — set/clear editor selection.
- `vultra.scene.move_entity` `{entity, mode: root|parent|before|after, parent?, sibling?}` — reparent/reorder.
- `vultra.scene.instantiate_asset` `{asset?|assetUuid?|uri?, parent?, name?, position?, rotation?, scale?, keep*?}` — instantiate a registered asset.
- `vultra.scene.save` `{uri?}` — save the current world as a scene.

### `vultra.editor.*` — editor automation / generic command bridge
- `vultra.editor.command` `{name, arguments?}` — run any editor command by name (the generic escape hatch).
- `vultra.editor.command_batch` `{commands: [{name, arguments?}], stopOnError?}` — batch commands, per-command undo history.
- `vultra.editor.back_to_launcher` `{}` — return editor to launcher mode.
- `vultra.editor.window` `{action: status|focus|fullscreen|resize|move|center|maximize|minimize|restore|decorated|resizable|visible|close, ...}` — window control.
- `vultra.editor.scene_view` `{mode?: 2d|3d|ui2d|view3d, tool?: select|move|rotate|scale|rect|transform}` — Scene View mode/tool.
- `vultra.editor.capture` `{outputFile?}` — capture the editor backbuffer to PNG.
- `vultra.editor.recording` `{action: start|stop|status, outputFile?, frameDirectory?, mode?: stream|frames, fps?, maxFrames?, ...}` — record the editor. **Deferred.**
- `vultra.editor.input` `{...ImGui events}` — inject mouse/keyboard/text input.
- `vultra.editor.quit` `{}` — request the editor window to close.

### `vultra.assets.*` — asset registry / import
- `vultra.assets.list` `{type?, query?, limit?}` — list the live asset registry.
- `vultra.assets.read` `{uri}` — read a text asset (service first, filesystem fallback).
- `vultra.assets.write` `{uri, text, allowWrite, reimport?}` — write a text asset under the project asset root (requires `allowWrite: true`).
- `vultra.assets.import` `{uri?|path?, force?}` — reimport an existing project asset.
- `vultra.assets.import_from_web` `{url, targetUri?|targetDirectory?, allowOverwrite?, reimport?, maxBytes?, timeoutSeconds?}` — download an asset into the project.
- `vultra.assets.import_package` `{path}` — import a `.vultrapackage`.

### `vultra.material_graph.*` — material graph tooling
- `vultra.material_graph.list_nodes` `{}` — list `.vmatnode.json` node descriptors.
- `vultra.material_graph.compile` `{uri, graph?, includeSource?}` — compile a `.vmatgraph.json` and return the surface + diagnostics.

## Conventions

- Prefer the typed `vultra.scene.*` tools over `vultra.editor.command` when one exists;
  fall back to the command bridge only for commands without a dedicated tool.
- Writes (`vultra.assets.write`, `vultra.project.create_empty`) require an explicit
  opt-in flag (`allowWrite` / `allowCreate`) — set it intentionally.
- Keep MCP scratch (PNG dumps, throwaway projects) under `build/.tmp/` and reclaim it
  with `tools/clean-mcp-tmp.{ps1,sh}` (see the root `AGENTS.md`).
- Runtime MCP tools are **live engine operations**, not a file-edit channel — read
  repository AI context from `ai/` directly, never through MCP.
