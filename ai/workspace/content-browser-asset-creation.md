# Content Browser Asset Creation

## Summary

Added an editor-side `ContentAssetRegistry` and wired the Content Browser
`Create` menu to registered asset creators. Built-ins now create `.vscn` scenes
and `.lua` scripts, write them under the current project asset directory,
reimport them through the asset service, and select the created file. Lua
scripts open in the Code Editor after creation.

## Verification

- `xmake build -y vultra-app` passed.

## Handoff

Future Lua editor plugins should register asset creators through an editor-owned
extension service that feeds `ContentAssetRegistry`, rather than directly
mutating Content Browser window state.

## Follow-Up: Scene Open Semantics

Adjusted Content Browser scene behavior so double-clicking a `.vscn` emits an
editor-level `OpenScene` command instead of opening the source in Code Editor.
`EditorApp` owns the actual world replacement and asks for confirmation when
the current scene is dirty, so the flow no longer depends on Scene Hierarchy
being open or drawn in a particular order. Single-clicking a scene still selects
the source asset, and Inspector exposes `Edit As Source` for explicit source
edits.

Verification:

- `xmake build -y vultra-app` passed.
- Replaced the window-local scene-open request with `pendingEditorCommands`.

## Follow-Up: Material Graph Open Command

Added `OpenMaterialGraph` to the editor command path. Double-clicking
`.vmatgraph` or `.vmatgraph.json` in the Content Browser now queues an editor
command, `EditorApp` records the requested graph URI and opens/focuses the
Material Graph window, and `MaterialGraphWindow` loads the requested graph for
editing.

Verification:

- `xmake build -y vultra-app` passed.

## Follow-Up: Scene Thumbnails

Scene View can now save a 128x128 PNG thumbnail of its current render target to
the project `.vultra/thumbs/` directory. EditorApp asks Scene View to save the
current scene thumbnail before switching scenes, returning to the launcher,
loading another project, or shutting down the editor. Empty or not-yet-rendered
views are skipped.

Verification:

- `xmake build -y vultra-app` passed.

## Follow-Up: Scene Thumbnail Display

Scene thumbnail paths are now shared by Scene View and Content Browser, so
`.vscn` grid items can load the saved PNG from `.vultra/thumbs/` instead of
falling back to the generic source icon. Scene View render targets are created
as `RGBA8_UNorm` so the existing readback path can capture them reliably before
writing thumbnails. Thumbnail generation center-crops the Scene View render
target to a square before resizing to 128x128, preserving aspect ratio while
allowing edge content to be discarded.

Verification:

- `xmake build -y vultra-app` passed.

## Follow-Up: Splash Scene Thumbnail Cook

Project splash loading now also prewarms `.vscn` thumbnails through the existing
asset thumbnail queue. The service scans project scene files outside
`resources/imported`, queues missing or source-newer-than-thumbnail previews,
instantiates each scene into the loading world, and renders a 128x128 offline
thumbnail with the universal renderer. Scene primary cameras are preferred; if a
scene has no camera, the cook falls back to a bounds preview camera that includes
mesh and Gaussian splat content.

Verification:

- `xmake build -y vultra-app` passed.

## Follow-Up: Material Graph Thumbnails

Material graph files (`.vmatgraph` and `.vmatgraph.json`) now use the asset
thumbnail pipeline instead of generic source icons. During splash prewarm, the
thumbnail service renders a built-in sphere with the graph applied as a slot 0
material override, using the universal renderer and the same preview lighting as
other offline render thumbnails. Content Browser grid items load the generated
PNG when available. Saving a material graph now also writes the current material
sphere preview render target back to the same thumbnail cache path and marks it
ready in the thumbnail service, so edits refresh previews during the same editor
session.

Verification:

- `xmake build -y vultra-app` passed.

## Follow-Up: Editor Command Cleanup

Open-style editor actions now have a small helper layer in
`editor_app/editor_commands.hpp` so UI windows can enqueue semantic commands
(`queueOpenScene`, `queueOpenRenderGraph`, `queueOpenMaterialGraph`) instead of
manually constructing `AppState::EditorCommand` payloads. Content Browser uses
the helpers for scene/render graph/material graph opens and code editor focus.
Game View's "Create Camera" empty-state action now labels the next scene history
entry, so the operation appears as "Create Primary Camera" in History.

Verification:

- `xmake build -y vultra-app` passed.
