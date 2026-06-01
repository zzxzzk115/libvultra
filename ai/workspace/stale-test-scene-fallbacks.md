# Stale Test Scene Fallbacks

## Summary

- Removed runtime/editor defaults that silently fell back to `res://scenes/test.vscn`.
- New project templates now create and reference `res://scenes/main.vscn`.
- Existing projects with a missing or empty `default_scene` now prefer the package `entry_scene`, then the first available `.vscn` under `resources/scenes/`.
- The runtime binding probe no longer calls the deleted `test.vscn`; it probes the current example package scene instead.
- Updated the example package entry scene to `res://scenes/sponza.vscn`.

## Root Cause

The log line:

```text
[SceneSystem] Failed to load scene text asset: res://scenes/test.vscn
```

was triggered after `sponza.vscn` loaded because `resources/scripts/binding_probe.lua` still called `Scene.load("res://scenes/test.vscn")`.
There were also stale defaults in editor/project helper code that could reintroduce the deleted scene path in new or reset projects.

## Verification

- `rg -n "res://scenes/test\\.vscn|test\\.vscn" resources source/vultra_app tools`
  - No matches.
- `git diff --check`
  - Passed.
- `xmake build -y vultra-app`
  - Passed.
