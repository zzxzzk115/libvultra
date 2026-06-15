# Getting Started with VultraEngine

**English** | [简体中文](zh_CN/getting_started_CN.md)

Welcome! This guide gets you from a fresh build to your first running scene. It
covers what the `vultra` executable does, how to launch it, and how to create or
open a project in the editor.

> Looking to build the engine from source? See [BUILD.md](../BUILD.md).

## What you get: one executable, many roles

VultraEngine ships as a single executable. The build target is `vultra-app`, and
the produced binary is named `vultra` (`vultra.exe` on Windows). That one program
is your:

- **Project Launcher** — create, open, and manage projects.
- **Editor** — author scenes, materials, render graphs, and scripts.
- **Runtime player** — run a project or a packaged game.
- **Tool host** — integrated asset importer, packer, and shader compiler (run as
  subcommands; see below).

How it decides which role to play depends on what it finds at startup:

- **Editing from loose files.** When you open a project (a directory containing a
  `.vproject` file), the editor reads your assets directly from the project's
  asset root (`resources/` by default) as ordinary files on disk. Edit a scene,
  shader, or material and the change is right there on disk — ideal for iteration.
- **Running a packaged `resources.vpk`.** For distribution, a project is exported
  into a `.vpk` package (a bundled, read-only virtual filesystem mounted at
  `res://`). At launch the runtime looks for a VPK — first `vultra.vpk` next to
  the executable, then `resources.vpk` in the working directory or project — and
  if it finds one, it boots straight into runtime mode and plays the packaged
  game. No editor, no loose files.

If no VPK is found and no project is specified, `vultra` opens the Project
Launcher.

## Launching: CLI modes and flags

Run `vultra help` (or `vultra --help`) to print usage. The flags below are grouped
by purpose.

### Editor / runtime

- `--editor` — start in editor mode. **Requires `--project`**; an editor session
  without a project is invalid (it falls back to the launcher).
- `--project <dir-or-.vproject>` — the project to open. Accepts the project
  directory or the `.vproject` file inside it.
- `--scene <res://...>` — the scene to load (e.g. `res://scenes/main.vscn`). When
  omitted, the project's default scene is used, falling back to
  `res://scenes/main.vscn`.
- `--vpk <file>` — load a specific `.vpk` package and run it.
- `--render-mode <visible|offscreen|none>` — `visible` (default) shows a window;
  `offscreen` hides the window but keeps rendering active (useful for capture);
  `none` runs headless simulation with no rendering.
- `--plugins-dir <dir>` — in runtime mode, discover and enable every plugin in
  this directory. (In editor mode, the project's enabled-plugins list drives
  loading instead.)

> `--backend` / `--render-backend` and `--render-profile` are accepted on the
> command line but are currently placeholders — they do not change behavior yet.

### XR

- `--xr` / `--no-xr` — request or disable XR. XR is only supported on the Vulkan
  backend; requesting it elsewhere logs a warning.
- `--xr-mirror` / `--no-xr-mirror` — toggle mirroring the headset view to the
  desktop window.

### Debug

- `--validation` / `--no-validation` — enable or disable graphics validation
  layers.
- `--debug-markers` / `--no-debug-markers` — toggle GPU debug markers.
- `--renderdoc` / `--no-renderdoc` — toggle RenderDoc integration.

### Automation (RPC / MCP)

- `--mcp` — enable the agent and auto-start the MCP server so
  external tools can drive the editor/runtime.
- `--mcp-host <host>` / `--mcp-port <port>` — bind address and port for the MCP
  server (default port `8848`).

Combine with `--render-mode none` to run no-window simulation services (visual
capture tools are disabled), or `--render-mode offscreen` to keep render services
active for capture.

### Export

- `--export` — run a headless desktop export (no editor window) and exit.
- `--export-output <dir>` (alias `--out`) — destination directory for the export.
- `--export-platform <platform>` — target platform; empty means the host desktop.
- `--export-run` — launch the exported build after packaging.

See [Cross-platform export](cross_platform_export.md) for the full workflow.

### Integrated tools (subcommands)

These bypass the engine UI entirely:

- `vultra asset import <asset-root> [--reimport]` — import/cook source assets into
  `<asset-root>/imported`.
- `vultra asset pack <asset-root> <out.vpk> [...]` — bundle imported assets into a
  `.vpk`.
- `vultra asset validate-vpk <resources.vpk> [...]` — verify a `.vpk` against its
  source tree.
- `vultra shader <args>` — run the shader compiler CLI (`vultra shader --help`).

### Example invocations

```sh
# Open the launcher (no args)
vultra

# Edit a project
vultra --editor --project ./MyGame

# Edit, with the MCP automation server on a custom port
vultra --editor --project ./MyGame --mcp --mcp-port 9000

# Play a project's scene directly (no editor)
vultra --project ./MyGame --scene res://scenes/main.vscn

# Run a packaged game
vultra --vpk ./MyGame.vpk

# Headless simulation for automation
vultra --mcp --render-mode none --project ./MyGame

# Headless desktop export
vultra --export --project ./MyGame --export-output ./build --export-run
```

## Creating or opening a project

Launch `vultra` with no arguments to open the **Project Launcher**. From there you
can create a new project, open an existing one, or fork a sample.

A project is a directory containing a `.vproject` workspace file. Creating a new
project scaffolds:

- `resources/` — the asset root (configurable via the project's `assetRoot`),
  containing a starter `scenes/main.vscn`. A full starter additionally lays down a
  default render graph (`render/default.vrg.json`), a sample material graph
  (`materials/default.vmatgraph.json`), a project shader library, and a couple of
  example post-process passes (Pixelate, Invert).
- `ai/` — a tracked AI-collaboration workspace (`game.md` brief plus `specs/`,
  `tasks/`, `workspace/`, `knowledge/`, `agents/`, and `generated/` folders) for
  recording project intent and conventions.

The `.vproject` itself records the project name, asset root, default and build
scenes, the editing render graph, and which plugins are enabled.

New projects open with a minimal scene already wired up — a directional light, a
camera, and an environment node — so you have something to render immediately.

For more starters, the [vultra-examples](https://github.com/zzxzzk115/vultra-examples)
repository has additional sample projects you can clone or fork from the launcher.

## A quick tour of the editor

Once a project is open in editor mode, you get a dockable, multi-window workspace.
The main windows include:

- **Scene View** — the 3D viewport for navigating and editing your scene; with a
  separate **Game View** that shows the scene through the active camera.
- **Scene Hierarchy** — the tree of entities in the current scene.
- **Inspector** — view and edit the components on the selected entity.
- **Content Browser** — browse and manage the project's assets.
- **Render Graph Editor** — author the node-based rendering pipeline.
- **Material Graph Editor** — build materials as node graphs.
- **Animator Graph** — author animation state machines.
- **Frame Debugger** — inspect render passes and resources frame by frame.
- **Profiler** — track frame timing and performance.
- **Console** — engine and script log output.
- **History** — undo/redo history for editor actions.
- **Code Editor** — edit project scripts in-app.

The editor is also extensible: plugins can register their own panels and menu
items (see [Plugins](plugins.md)).

## Next steps

- [Projects & assets](project_and_assets.md) — project layout, the asset
  pipeline, and `res://` URIs.
- [Scenes & components](scene_and_components.md) — the entity/component model and
  how scenes are built.
- [Lua scripting](lua_scripting.md) — write gameplay logic with the Lua API.
- [Render graphs](render_graphs.md) — author and customize the rendering pipeline.
- [Plugins](plugins.md) — extend the engine and editor.
- [Cross-platform export](cross_platform_export.md) — package and ship your game.
