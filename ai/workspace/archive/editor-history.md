# Editor History

## Scope

- Added an editor-level `EditorHistory` service using command/history state snapshots.
- Scene state snapshots are serialized through the existing `.vscn` reader/writer path, so undo, redo, and history restore operate on the same scene data model as save/load.
- Added `Ctrl+Z` undo and `Ctrl+Shift+Z` redo in editor mode.
- Added a `History` editor window docked under `Inspector` in the default right panel.

## Recorded Actions

- Scene changes are observed automatically after editor windows draw, so legacy UI mutations still enter the stack.
- Explicit labels are provided for common key actions:
  - create, delete, rename, move, reparent, lock, show/hide entities
  - instantiate dropped scene assets
  - add/remove components
  - edit transform, mesh, splat, environment, reflection probe, light, camera, XR view, and script components
  - scene view gizmo transforms

## Verification

- `xmake build -y vultra-app` passed.
