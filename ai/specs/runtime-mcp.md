# Runtime MCP

## Intent

Vultra editor exposes a C++ runtime MCP server for local automation and
diagnostics while the engine is running. Repository/project AI Harness context
is read directly from tracked `ai/` files; Runtime MCP is the only maintained
MCP surface.

## Transport

- The server runs inside `vultra-app`.
- It listens on localhost only, default `127.0.0.1:8848`.
- It uses HTTP request/response JSON-RPC for MCP methods.
- V1 supports `initialize`, `tools/list`, and `tools/call`.
- Editor settings can start the server, and CLI `--mcp` forces it on for
  debugging regardless of saved settings.

## Threading

The network thread must not touch engine, renderer, world, or editor state.
Tool calls are queued to the editor main thread, executed during editor ticks,
and completed back to the waiting HTTP request.

## Editor Command Model

Editor automation must be command-driven. UI widgets and Runtime MCP should
submit the same editor command names with structured JSON arguments; the editor
executes those commands on the main thread. MCP wrappers may remain for
ergonomic compatibility, but their implementation should delegate to the shared
command path whenever the action is also available to users in the UI.

The first shared commands are:

- `editor.new_scene`
- `editor.save_scene`
- `editor.back_to_launcher`
- `editor.undo`
- `editor.redo`
- `editor.history`
- `editor.build_and_run`
- `runtime.playback`

Runtime MCP exposes the generic `vultra.editor.command` tool:

```json
{
  "name": "runtime.playback",
  "arguments": { "action": "play" }
}
```

For multi-step MCP operations, use `vultra.editor.command_batch`. The batch is
only an MCP transport envelope. It must execute editor commands one by one and
preserve the history behavior of each atomic command; it must not collapse the
whole MCP request into a single undo step.

History entries belong to editor commands, not MCP tools. A single MCP tool may
run zero, one, or many history-producing editor commands. Query/IO commands
should not create history entries; scene-editing commands should create their
own atomic history boundaries.

New editor features should first add/extend an editor command, then bind UI and
MCP to that command. Direct UI-only mutation is technical debt unless it is a
pure local widget state change.

## V1 Tools

- `vultra.runtime.status`
- `vultra.project.info`
- `vultra.editor.command`
- `vultra.editor.back_to_launcher`
- `vultra.editor.window`
- `vultra.project.create_empty`
- `vultra.scene.new`
- `vultra.scene.list_entity_kinds`
- `vultra.scene.list_component_kinds`
- `vultra.scene.add_entity`
- `vultra.scene.remove_entity`
- `vultra.scene.add_component`
- `vultra.scene.update_component`
- `vultra.scene.remove_component`
- `vultra.scene.instantiate_asset`
- `vultra.scene.save`
- `vultra.assets.list`
- `vultra.assets.read`
- `vultra.assets.write`
- `vultra.material_graph.list_nodes`
- `vultra.material_graph.compile`
- `vultra.assets.import`
- `vultra.assets.import_from_web`
- `vultra.assets.import_package`
- `vultra.editor.capture`
- `vultra.editor.recording`
- `vultra.editor.input`
- `vultra.editor.quit`
- `vultra.runtime.playback`
- `vultra.runtime.profiler`
- `vultra.runtime.framegraph_snapshot`
- `vultra.runtime.frame_resources`
- `vultra.runtime.scene_context`
- `vultra.runtime.rendergraph_source`
- `vultra.runtime.dump_frame_textures`
- `vultra.runtime.reload_pipeline`
- `vultra.runtime.capture_frame`

Runtime tools return structured JSON payloads inside MCP text content. Service
unavailability is reported as an MCP tool error payload rather than a crash.

PowerShell test helpers should avoid `$args` as a function parameter for MCP
tool arguments. It is a PowerShell automatic variable and can turn
`tools/call.params.arguments` into an array. Use `$toolArgs` or another explicit
name in examples and smoke scripts.

`scene_context` reports the currently opened scene, dirty state, all camera
components, the selected main camera (primary camera first, otherwise highest
priority), and the render graph URI mapped from that camera's renderer key.
`rendergraph_source` reads graph text through the asset service.

`dump_frame_textures` sends a render-service dump request rather than driving
editor UI. The render system keeps the request alive for multiple frames,
ignores UI thumbnail overrides for matching resources, creates `DebugCapture/`
RGBA8 preview textures at requested resolution, and MCP writes those textures
to PNG files. The request supports filter/camera/renderer narrowing so full-res
capture does not allocate every debug texture at once.

## V2 Automation Surface

Runtime MCP also exposes editor/project automation helpers:

- `vultra.project.info` reports the loaded project, asset root, default scene,
  render graph, generation counters, and `.vproject` path.
- `vultra.editor.back_to_launcher` returns the editor shell to launcher mode
  while keeping Runtime MCP alive for follow-up automation.
- `vultra.editor.window` reports and controls the editor window using the
  cross-platform window abstraction. Supported actions include `status`,
  `fullscreen`, `resize`, `move`, `center`, `maximize`, `minimize`, `restore`,
  `decorated`, `resizable`, `visible`, and `close`.
- `vultra.project.create_empty` creates a local project and opens it in editor
  mode. `template="empty"` is the default and writes only project metadata,
  package manifest, and `resources/scenes/main.vscn`. `template="minimal"` writes
  the smallest useful editor project: main scene, material graph, render graphs,
  pass scripts, project shader library, shader sources, and project AI docs.
- `vultra.scene.new` clears the current world, optionally adding a default sun,
  main camera, and environment.
- `vultra.scene.list_entity_kinds` and `vultra.scene.list_component_kinds`
  report supported editor automation templates and components.
- `vultra.scene.add_entity` creates entity templates such as `empty`,
  `primitive`, `camera`, `light`, and `environment`.
- `vultra.scene.add_component`, `vultra.scene.update_component`, and
  `vultra.scene.remove_component` mutate components atomically. Tests should
  create primitives by adding an entity with `entity_kind=primitive`, then add
  physics and shape components explicitly.
- `vultra.scene.remove_entity` removes an entity and its children.
- `vultra.scene.instantiate_asset` instantiates a registered asset by UUID or
  `res://` URI through the same asset-to-scene implementation used by Content
  Browser drag-and-drop. It supports parent/sibling placement, optional
  transform overrides, and mesh sub-asset transform channel choices.
- `vultra.scene.save` saves the current world through `ISceneService`.
- `vultra.assets.list` reads the live asset registry and supports `type`,
  `query`, and `limit` filters.
- `vultra.assets.read` reads text assets through the asset service, falling back
  to filesystem reads under the current project asset root.
- `vultra.assets.write` writes text assets only under the current project asset
  root and requires `allowWrite=true`; it can optionally reimport the written
  asset.
- `vultra.material_graph.list_nodes` scans `.vmatnode.json` assets under the
  current project asset root and returns parsed project-defined Material Graph
  node descriptors plus diagnostics. Use it to verify editor-visible custom
  node discovery without relying on manual menu inspection.
- `vultra.material_graph.compile` reads a project `.vmatgraph.json`, loads
  project `.vmatnode.json` descriptors into the same registry shape used by the
  editor, and runs the surface material graph compiler. It can return generated
  source with `includeSource=true` for smoke tests that need to confirm custom
  node snippets were expanded.
- `vultra.assets.import` reimports an existing project asset through
  `IAssetService::reimportAsset`.
- `vultra.assets.import_from_web` downloads an `http://` or `https://` URL into
  the current project asset root, optionally reimporting it. The target must
  resolve under the project asset root; use `targetUri` for an exact
  destination or `targetDirectory` to keep the URL filename. The v1 downloader
  uses the cross-platform `curl` executable and returns a structured error when
  `curl` is unavailable or the download fails.
- `vultra.assets.import_package` imports a `.vultrapackage` into the current
  project asset root and queues source paths for the editor background importer.
- `vultra.editor.capture` saves the current editor backbuffer to a PNG.
- `vultra.editor.recording` records the editor backbuffer on the main thread.
  The default `mode=stream` opens `ffmpeg` as a stdin rawvideo sink and writes
  BGRA/RGBA frame bytes directly to the encoder through a writer thread and a
  small bounded queue, avoiding per-frame PNG compression and disk IO. Queue
  overflow drops recording frames instead of blocking editor ticks.
  `mode=frames` remains available as a PNG sequence fallback and can encode on
  stop when `ffmpeg` is available. Windows uses `_popen/_pclose`; Linux and
  macOS use `popen/pclose`.
- `vultra.editor.input` injects basic ImGui mouse, wheel, text, and key events
  for UI automation. It is not a full OS/window event injection layer yet; that
  should be added as a dedicated engine/editor event service before tests depend
  on camera/gameplay input semantics.
- `vultra.editor.quit` requests the editor window to close through
  `IWindowService`; agents should prefer it over externally terminating the
  process when ending an automation run.

Runtime MCP is an editor shell service: when explicitly enabled, it must remain
available in both Project Launcher and Editor modes so agents can automate
launcher-to-project workflows without falling back to UI input or process
control.

Asset mutation tools must stay scoped to the current project asset root. They
must reject paths outside that root and should prefer structured services
(`IAssetService`, package import helpers, render services) over UI automation.
