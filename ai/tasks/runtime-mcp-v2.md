# Runtime MCP V2 Automation

## Goal

Extend the embedded C++ Runtime MCP server from diagnostics into editor/runtime
automation so agents can test real editor workflows while `vultra-app` is
running.

## Scope

- Report loaded project metadata and editor generation counters.
- Keep Runtime MCP alive in both Project Launcher and Editor shell modes.
- Introduce a shared editor command model used by both UI and MCP.
- Return to the Project Launcher and create empty projects from MCP.
- Create, populate, and save scenes from MCP.
- Inspect the live asset registry.
- Read and write text assets under the current project asset root.
- Reimport project assets through `IAssetService`.
- Import `.vultrapackage` archives into the current project.
- Capture the current editor backbuffer to an image file.
- Record the editor backbuffer directly to an ffmpeg rawvideo stream, with PNG
  frame sequence fallback.
- Keep stream recording cross-platform by using platform pipe APIs and a bounded
  writer queue so encoder backpressure does not stall editor ticks.
- Inject basic ImGui mouse, wheel, text, and key events for UI automation.
- Request a clean editor window close through MCP.

## Safety

- Asset writes require `allowWrite=true`.
- Asset paths must resolve inside the current project asset root.
- Runtime MCP should use editor/engine services directly instead of driving UI
  panels for data extraction.
- `vultra.editor.input` is currently an ImGui-level helper. Full window/gameplay
  event simulation should be implemented as a dedicated input event service.
- Automation runs should use `vultra.editor.quit` for normal shutdown instead
  of externally killing the editor process.
- Launcher/project automation should use semantic MCP tools instead of simulated
  clicks.
- UI code and MCP wrappers should delegate user-visible behavior to shared
  editor commands. Direct mutation should be reserved for local widget state or
  temporary migration work.
- MCP batch requests are transport envelopes, not history commands. Each
  history-producing editor command inside a batch must keep its own undo/redo
  boundary.

## Verification

- `xmake build -y vultra-app`
- Start `vultra-app --editor --mcp --project example.vproject --no-xr`
- Smoke test `tools/list`, `vultra.project.info`, `vultra.assets.list`, and
  `vultra.editor.capture` over `POST /mcp`.
- Smoke test `vultra.editor.quit` and confirm the editor process exits.
- Smoke test `vultra.editor.recording` with `start`, `status`, and `stop` in
  `mode=stream`, confirming an mp4 is written without per-frame PNG output.
  Confirm queue depth and dropped-frame counters are reported. Keep
  `mode=frames` available for fallback diagnostics.
- End-to-end MCP workflow smoke:
  - Start an existing editor project.
  - Return to launcher with `vultra.editor.back_to_launcher`.
  - Create an empty project with `vultra.project.create_empty`.
  - Create a scene with `vultra.scene.new`.
  - Add a dynamic smooth sphere and a static plane with `vultra.scene.add_entity`,
    then attach/update rigid body and shape components with component commands.
  - Save, play for five seconds, stop, save, and quit through MCP.
- Command-model smoke:
  - `vultra.editor.command` executes `runtime.playback`.
  - Existing `vultra.runtime.playback` wrapper delegates to the shared command.
  - `vultra.editor.command` executes `editor.back_to_launcher` and MCP remains
    available in launcher mode.
  - `vultra.editor.command_batch` with multiple scene edits produces multiple
    history entries, and one undo only reverts the latest atomic edit.
