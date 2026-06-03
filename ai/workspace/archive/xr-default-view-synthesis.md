# XR default view synthesis handoff

Date: 2026-05-27

## Scope

- Added a `default_xr` render graph that inserts `XrViewSynthesis` before selection outline/final composition.
- Added `XrViewSynthesisPass` as a graph-facing source-forwarding stub.
- Added graph parameters for future warping and inpainting backend selection.
- Reserved the default backend names `adaptive_mesh_graphics` and `pull_push`.
- Replaced the source-forwarding stub with registered warping and inpainting
  backends plus shader-backed default implementations.

## Current behavior

- `XrViewSynthesis` is enabled in `default_xr.vrg.json` and feeds the
  selection outline/final composition path.
- The C++ pass now resolves registered warping and inpainting backend names.
- Registered warping backends: `adaptive_mesh_graphics` and `none`.
- Registered inpainting backends: `pull_push` and `none`.
- `adaptive_mesh_graphics` now builds an adaptive screen-space mesh in compute
  and rasterizes that generated vertex buffer in a graphics pass. Subdivision
  is selected per coarse cell from local depth-disparity, depth discontinuity,
  and screen-span demand instead of a binary global-grid decision.
- For one-eye synthesis directions such as left-to-right, the source eye layer
  is copied directly from the source texture in the raster fragment pass; only
  the target eye layer is warped and repaired.
- Target-eye color is sampled in the raster fragment pass from interpolated
  source UVs. The adaptive mesh no longer relies on interpolated vertex colors,
  which caused coarse cells to blur and smear after subdivision.
- `pull_push` now mirrors the reference structure more closely: it computes the
  mip count from the warped image size, initializes mip0, loops `push` passes
  down the mip chain, then loops `pull` passes back up to repair invalid
  regions at full resolution.
- Warped and pull/push temporary render targets clear to transparent black so
  uncovered pixels remain invalid for the alpha-based repair path.
- Vulkan texture blits now compute src/dst offsets from the requested mip level
  and copy all common array layers. The pull/push pyramid depends on blitting
  to non-zero mips, so using the base extent for every mip could produce invalid
  GPU commands and device loss.
- Adaptive mesh defaults use 8x subdivision of a 16px coarse cell, giving 2x2
  pixel quads in high-detail regions without the severe vertex-count spike from
  16x subdivision. The compute shader still uses 9-point depth/disparity
  sampling and local subquad validation.
- The raster vertex shader now discards inactive adaptive-mesh vertices before
  source-eye preservation. Without that guard, inactive quads were promoted to
  valid source-eye geometry and could cause left-eye flicker.
- The target-eye warp sign is `sourceEye - targetEye`: left-to-right moves
  foreground samples left in screen space, exposing disocclusion holes on the
  right side of foreground objects.
- RHI mip-size calculation now clamps each dimension to at least 1. Without
  this, non-power-of-two stereo targets could compute a zero-sized final mip
  dimension for blit regions even though the pull/push pyramid requested a
  1x1 final level.
- Pull/push per-level passes/resources now include the mip level and target
  extent in their debug names, e.g. `XRViewSynthesisPush[L10 1x1]`. Frame
  Debugger previews the captured texture extent from the frame graph resource;
  it does not expose individual mip levels of the shared pyramid texture.
- Runtime Graph browsing captures low-resolution thumbnails by default and
  switches the selected texture to full-resolution capture only when its
  preview window is opened.

## Extensibility

- `sourceView` and `targetView` are graph parameters backed by registered
  view-role options.
- Warping and inpainting backend names are graph parameters. New implementations
  can be registered behind the pass later without changing graph files.
- Render Graph editor controls now present comboboxes with labels from the
  registered view-role and backend catalogs.
- `default_xr` is a selectable render graph asset. Runtime code does not
  special-case it; projects must select/apply the graph through the existing
  render pipeline path.

## Verification

- `xmake build -y vultra-app` passed.
- Re-ran `xmake build -y vultra-app` after enabling DirectGBuffer multiview for mesh instances.
- Re-ran `xmake build -y vultra-app` after making unavailable OpenXR systems fall back to standalone Vulkan.
- Re-ran `xmake build -y vultra-app` after removing diagnostic cleanup that hid XR interop failures.
- Re-ran `xmake build -y vultra-app` after replacing the synthesis stub with registered warping/inpainting backends and shaders; passed.
- Re-ran `xmake build -y vultra-app` after switching
  `adaptive_mesh_graphics` to compute-generated adaptive mesh rasterization;
  passed.
- Re-ran `xmake build -y vultra-app` after moving graph-node dropdowns to
  registry-provided labels; passed.
- Re-ran `xmake build -y vultra-app` after enabling the `default_xr` graph pass
  wiring while keeping renderer selection on the existing project/config path;
  passed.
- Re-ran `xmake build -y vultra-app` after making adaptive mesh subdivision
  depth-disparity driven and preserving the source eye in multiview synthesis;
  passed.
- Re-ran `xmake build -y vultra-app` after replacing the local neighborhood
  repair path with the pull/push pyramid; passed.
- Re-ran `xmake build -y vultra-app` after making the source eye an exact
  source-texture copy in the raster pass; passed.
- Re-ran `xmake build -y vultra-app` after moving target-eye color sampling to
  the raster fragment pass and extending pull/push coverage; passed.
- Re-ran `xmake build -y vultra-app` after changing pull/push to an image-size
  mip-chain plus push/down and pull/up loops following the reference; passed.
- Re-ran `xmake build -y vultra-app` after clearing warped and pull/push
  temporary targets with invalid alpha; passed.
- Re-ran `xmake build -y vultra-app` after fixing Vulkan mip blit extents and
  layered blits; passed.
- Re-ran `xmake build -y vultra-app` after reducing adaptive defaults back to
  8x subdivision and discarding inactive vertices before source-eye rendering;
  passed.
- Re-ran `xmake build -y vultra-app` after fixing the left-to-right warp
  direction and preserving source-eye invalid quads while still discarding
  inactive placeholder vertices; passed.
- Re-ran `xmake build -y vultra-app` after fixing mip-size calculation for
  non-power-of-two pull/push pyramid levels; passed.
- Re-ran `xmake build -y vultra-app` after adding per-level pull/push debug
  names for mip extent inspection; passed.
- Re-ran `xmake build -y vultra-app` after changing Runtime Graph texture
  capture to low-resolution thumbnails plus selected full-resolution preview;
  passed.

## Notes

- `XR_ERROR_FORM_FACTOR_UNAVAILABLE` from `xrGetSystem` now disables OpenXR for the current session instead of aborting startup.
- OpenXR Vulkan instance creation failures are no longer swallowed by a standalone Vulkan fallback; only unavailable XR systems degrade to non-XR.
- Frame Debugger initially showed `XrViewSynthesis` missing because XR rendering fell back to per-eye graphs whenever scene mesh instances existed. The highend DirectGBuffer path now has a multiview vertex shader variant and reads the stereo camera block, so single-graph stereo can be used with mesh instances.
- Frame Debugger texture capture now splits stereo array textures into per-eye entries.
- `resources/scenes/test.vscn` was already dirty and was not touched for this task.
- No commit was made.
