# Editor Startup Splash Import Progress

Date: 2026-06-03

## Issue

Starting the editor with a project showed a black window before the splash UI.
The asset import scan runs before the editor can draw splash content, so making
the engine window visible at process startup exposed an empty frame.

## Fix

- Editor mode now starts with the OS window hidden. Launcher mode still starts
  visible.
- Editor project configuration disables the AssetSystem startup import scan.
  That startup scan happened during engine subsystem initialization, before the
  editor splash state machine could draw anything, and produced the long
  `[vasset] ...` log burst before the splash window existed.
- Releasing editor state for a project load explicitly hides any previous
  editor shell window.
- Project loading now releases old editor state, applies the splash window, and
  waits one loading tick before starting the async asset import task. This gives
  the splash overlay a frame to present before the import scan can consume CPU.
- The existing import task progress callback continues to update
  `m_ImportProgress`; `ImportAssets` copies that progress into the splash
  loading overlay while scan/import runs.

## Verification

- `xmake build -y vultra-app` passed.
- `git diff --check` passed.

## Follow-Up

Use a direct editor launch for visual confirmation:

```text
xmake run vultra-app --editor --mcp --project example.vproject --no-xr
```

The expected behavior is no visible black window during the initial bootstrap
gap, followed by the splash loading window before asset scan/import starts. The
splash should present at least one frame before vasset import begins, then the
detail text and progress bar should update from vasset async scan/import
progress.
