# Runtime MCP V1 Verification

## Result

- Implemented C++ Runtime MCP server embedded in `vultra-app`.
- Server starts from editor AI Agent settings and binds to localhost.
- `--mcp` starts the server regardless of saved editor settings.
- Tool calls are queued from the HTTP thread and executed on the editor main
  thread.
- Runtime MCP is the maintained MCP surface; repository/project Harness context
  is read directly from tracked `ai/` files.

## Verification

- `xmake build -y vultra-app` passed on Windows x64 release.
- Runtime smoke passed by launching `vultra-app --editor --mcp --mcp-port
  8848 --project example.vproject --no-xr`, connecting over HTTP MCP, and
  calling `initialize`, `tools/list`, `vultra.runtime.status`, and
  `vultra.runtime.playback`.
- Follow-up runtime smoke verified:
  - `vultra.runtime.scene_context` returned open scene
    `res://scenes/sponza.vscn`, main camera `Camera`, renderer key `default`,
    and render graph `res://render/default.vrg.json`.
  - `vultra.runtime.rendergraph_source` loaded `res://render/default.vrg.json`
    and returned 5970 bytes of source text.
  - `vultra.runtime.frame_resources` returned 13 debug texture/resource
    metadata entries after frame graph texture capture was enabled.
  - `dump_frame_textures` routes through
    `IRenderService::requestFrameGraphTextureDumpCapture`; `RenderSystem`
    handles matching/filtering before creating full-resolution `DebugCapture/`
    textures.
  - The capture path is independent of Frame Debugger / Runtime Graph UI. MCP
    injects a short-lived manual render camera based on the current scene main
    camera, lets RenderSystem run the selected renderer, and then reads the
    full-resolution debug capture texture.
  - `frame_resources` uses the same bottom-level capture path, so it can report
    resource metadata even when no UI viewport is driving the frame graph.
  - Smoke confirmed `vultra.runtime.frame_resources` returned
    `DirectGBufferColor` metadata at `1280x720`, renderer `default`.
  - Smoke confirmed `vultra.runtime.dump_frame_textures` wrote
    `.vultra/mcp/frame_textures/Camera_default_DirectGBufferColor_Camera_DirectGBufferColor_layer_0_1280x720_eRGBA8_UNorm_0.png`.
    The PNG was verified as `1280x720`, 1,664,240 bytes.
- The smoke test connected on the requested port `8848`; the server keeps the
  requested port and actual listening port separate so fallback does not cause
  restart loops.
- V2 automation smoke passed after `xmake build -y vultra-app` by launching
  `vultra-app --editor --mcp --project example.vproject --no-xr` and calling:
  - `tools/list`, which returned project, asset, editor capture, and editor
    input tools.
  - `vultra.project.info`, which returned project `example`, asset root
    `resources`, default scene `res://scenes/sponza.vscn`, and editing render
    graph `res://render/default.vrg.json`.
  - `vultra.assets.list` with `limit=5`, which returned 5 of 218 live registry
    entries.
  - `vultra.assets.read` for `res://render/default.vrg.json`, which loaded the
    render graph through the asset service.
  - `vultra.editor.capture`, which wrote
    `.vultra/mcp/editor_frame_v2.png` at `1280x720`, 585,728 bytes.
  - `vultra.editor.input` with `mouse_move`, which returned `ok=true` for the
    ImGui input target.
- `vultra.editor.quit` smoke passed: the tool appeared in `tools/list`, returned
  `ok=true` / `closing=true`, and the editor process exited through the window
  close path.
- Launcher-to-editor automation smoke passed after extending Runtime MCP to
  drain commands in both Launcher and Editor modes:
  - Started `vultra-app --editor --mcp --project example.vproject --no-xr`.
  - Returned to launcher with `vultra.editor.back_to_launcher`.
  - Created blank project
    `.vultra/mcp_projects/physics_plane_demo_20260601_144033`.
  - Created scene `res://scenes/mcp_plane_physics_demo.vscn`.
  - Added `Smooth Dynamic Ball` with builtin sphere mesh, dynamic rigid body,
    and `SphereShapeComponent/radius = 0.5`.
  - Added `Static Ground Plane` with builtin plane mesh, horizontal transform
    rotation, static rigid body, and `BoxShapeComponent/halfExtents = (5, 0.05,
    5)`.
  - Saved the scene, played for five seconds, stopped, saved again, and quit
    with `vultra.editor.quit`.
  - Verified the editor process exited and the saved scene contains the expected
    mesh, rigid body, and shape components.
- Editor recording smoke passed:
  - `ffmpeg -version` found `ffmpeg version 8.1-full_build-www.gyan.dev`.
  - Started `vultra.editor.recording` at 10 fps with output
    `.vultra/mcp/recordings/smoke_recording.mp4`.
  - Entered play mode, waited about three seconds, and `status` reported 25
    captured frames.
  - `stop` encoded the frame sequence with ffmpeg exit code 0.
  - Verified the mp4 exists at 157,715 bytes and the editor exited through
    `vultra.editor.quit`.
- Rawvideo stream recording smoke passed:
  - Started `vultra.editor.recording` with `mode=stream`, `fps=20`, and output
    `.vultra/mcp/recordings/stream_recording.mp4`.
  - Runtime MCP opened ffmpeg with `-f rawvideo -pixel_format bgra -video_size
    1280x720 -framerate 20 -i -`.
  - After about 3.1 seconds, `status` reported 49 streamed frames.
  - `stop` closed the ffmpeg pipe with exit code 0, produced a 171,925 byte mp4,
    stopped playback, and exited the editor through `vultra.editor.quit`.
- Writer-thread stream recording smoke passed after moving ffmpeg writes off the
  editor main thread:
  - Started `vultra.editor.recording` with `mode=stream`, `fps=24`,
    `maxQueuedFrames=4`, and output
    `.vultra/mcp/recordings/stream_thread_recording.mp4`.
  - After about 3.0 seconds, `status` reported 56 streamed frames, queue depth
    0, and dropped frames 0.
  - `stop` closed the writer thread/ffmpeg pipe with exit code 0 and produced a
    171,064 byte mp4.
- Shared command-model smoke passed:
  - `vultra.editor.command` executed `runtime.playback` with `play` and `stop`.
  - The compatibility wrapper `vultra.runtime.playback` executed `pause` through
    the same command path.
  - `vultra.editor.command` executed `editor.back_to_launcher`; runtime status
    then reported `mode = launcher`, and MCP remained available.
  - `vultra.editor.quit` exited the editor process normally.
- Command batch history-boundary smoke passed:
  - `vultra.editor.command_batch` executed `scene.new`, `scene.add_entity`, and
    component mutation commands in one MCP request.
  - `editor.history` reported separate scene/entity/component entries.
  - A single `editor.undo` moved back by one atomic command and left redo
    available, proving the MCP batch did not collapse multiple edits into one
    history command.
- No-project editor guard and empty-project MCP smoke passed:
  - Started `vultra.exe --editor --mcp --mcp-port 8861 --no-xr` with no
    `--project`.
  - `vultra.runtime.status` reported `mode = launcher`, proving no-project
    Editor mode is not entered.
  - `vultra.editor.window` reported a visible, resizable `1280x720` window.
  - `tools/list` exposed `vultra.project.create_empty` and did not expose the
    removed old project-creation wrapper.
  - `vultra.project.create_empty` created and opened
    `.vultra/mcp_projects/empty_mcp_smoke_20260601_8861` with
    `template = empty`.
  - Returned to Launcher with `vultra.editor.back_to_launcher`, then created
    and opened `.vultra/mcp_projects/empty_mcp_flow_20260601_8861`.
  - `vultra.editor.quit` exited the editor process normally.

## Manual Smoke Still Recommended

1. Launch `xmake run vultra-app`, or enable Agent Panel and Auto-start MCP
   Server in Editor Settings.
2. Send HTTP JSON-RPC MCP requests to `http://127.0.0.1:8848/`.
3. Verify `initialize`, `tools/list`, `vultra.runtime.status`, playback,
   profiler, frame graph, frame resources, pipeline reload, and frame capture
   responses.
