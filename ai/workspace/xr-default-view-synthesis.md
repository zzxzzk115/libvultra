# XR default view synthesis handoff

Date: 2026-05-27

## Scope

- Added a `default_xr` render graph that inserts `XrViewSynthesis` before selection outline/final composition.
- Added `XrViewSynthesisPass` as a graph-facing source-forwarding stub.
- Added graph parameters for future warping and inpainting backend selection.
- Reserved the default backend names `adaptive_mesh_graphics` and `pull_push`.

## Current behavior

- `XrViewSynthesis` is disabled in `default_xr.vrg.json`.
- The C++ pass forwards `source` unchanged when enabled.
- No adaptive warp or inpainting backend is active in this changeset.

## Extensibility

- `sourceView` and `targetView` are graph parameters so a future backend can support center-to-left/right or right-to-left without changing graph pass registration.
- Warping and inpainting backend names are graph parameters. New implementations can be registered behind the pass later without changing graph files.
- The planned `pull_push` backend is not implemented yet.

## Verification

- `xmake build -y vultra-app` passed.
- Re-ran `xmake build -y vultra-app` after enabling DirectGBuffer multiview for mesh instances.
- Re-ran `xmake build -y vultra-app` after making unavailable OpenXR systems fall back to standalone Vulkan.
- Re-ran `xmake build -y vultra-app` after removing diagnostic cleanup that hid XR interop failures.

## Notes

- `XrViewSynthesis` is currently disabled in `default_xr.vrg.json` while the experimental pass is cleaned up after device-lost failures.
- `XR_ERROR_FORM_FACTOR_UNAVAILABLE` from `xrGetSystem` now disables OpenXR for the current session instead of aborting startup.
- OpenXR Vulkan instance creation failures are no longer swallowed by a standalone Vulkan fallback; only unavailable XR systems degrade to non-XR.
- Frame Debugger initially showed `XrViewSynthesis` missing because XR rendering fell back to per-eye graphs whenever scene mesh instances existed. The highend DirectGBuffer path now has a multiview vertex shader variant and reads the stereo camera block, so single-graph stereo can be used with mesh instances.
- Frame Debugger texture capture now splits stereo array textures into per-eye entries.
- `resources/scenes/test.vscn` was already dirty and was not touched for this task.
- No commit was made.
