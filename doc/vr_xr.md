# VR / XR (OpenXR)

**English** | [简体中文](zh_CN/vr_xr_CN.md)

Vultra was originally built to support VR graphics research, and OpenXR is a
first-class part of the engine. This page describes how XR support works today,
how to enable it, and how the scene, cameras, and render graph drive the
headset.

> Scope note: this document only covers behaviour that is confirmed in the
> shipped C++/Lua source. XR is a large subsystem and some areas are still
> evolving; see [Areas to expand](#areas-to-expand) for what is intentionally
> left out.

## Overview

XR is implemented on the **Vulkan backend** using **OpenXR** with the Vulkan
graphics binding (`XR_USE_GRAPHICS_API_VULKAN`). The OpenXR `XRDevice` is created
only when the render device is requested with the `eXR` feature flag, and it is
created from the Vulkan backend's device-creation path
(`source/vultra/src/core/rhi/backends/vk/vulkan_render_device.cpp`). The OpenXR
instance, Vulkan instance/device, swapchain, and the per-frame view loop are all
wired through the Vulkan code path. There is no XR support on the WebGPU backend.

If no usable OpenXR runtime/headset is present, `XRDevice` reports itself as
unavailable and the engine **gracefully drops the `eXR` feature flag** and runs
flat (non-XR). XR is therefore safe to request even on machines without a
headset.

The default form factor is a head-mounted display with a
`PRIMARY_STEREO` view configuration (two eyes), and the default environment
blend mode is opaque (VR). Eye tracking is detected but optional
(`XRDeviceProperties::supportEyeTracking`).

Key source locations:

- `source/vultra/include/vultra/function/openxr/` and
  `source/vultra/src/function/openxr/` — `XRDevice`, `XRHeadset`, input, and the
  `XRRuntimeSystem` engine subsystem.
- `source/vultra/include/vultra/function/services/render_backend_service.hpp` —
  the `IRenderBackendService` XR API (`requestXRSession`, `isXREnabled`,
  `isXRMirrorEnabled`, `xrEyeViews()`, and the `XREyeView` struct).
- `source/vultra/src/function/rendering/backend/render_backend_system.cpp` — the
  per-frame XR swapchain acquire, per-eye view assembly, and mirror targets.

## Enabling XR

### Launch flags

The runtime parses these flags in
`source/vultra_app/src/launch_options.cpp`:

| Flag | Effect |
| --- | --- |
| `--xr` | Request XR for this session |
| `--no-xr` | Disable XR for this session |
| `--xr-mirror` | Enable the desktop mirror view (headset image shown on the monitor) |
| `--no-xr-mirror` | Disable the mirror view |

Example (from the usage banner):

```
vultra [--no-xr] [--xr-mirror|--no-xr-mirror] --editor --project <project-dir>
```

These flags set `LaunchOptions::xr` and `LaunchOptions::xrMirror`. Because XR
falls back gracefully, requesting `--xr` without a headset connected simply runs
flat.

### Config toggles

The engine config exposes an `XRConfig` block
(`source/vultra/include/vultra/core/engine/engine_context.hpp`,
`config.render.xr`):

| Field | Default | Meaning |
| --- | --- | --- |
| `mirror` | `true` | Whether the desktop mirror view is on |
| `autoStartSessionFromScene` | `true` | Let the scene start/stop the XR session automatically (see below) |
| `runtimeCameraOverride` | `false` | Force an XR session regardless of scene cameras |

### The mirror view

When XR and the mirror are both enabled, the engine maintains per-eye **mirror
targets** that copy the headset eye images so they can be displayed on the
desktop (e.g. in an ImGui panel or the editor). The render backend exposes the
mirror state via `IRenderBackendService::isXRMirrorEnabled()` and surfaces each
eye view's `mirrorTarget` through `xrEyeViews()`. The examples include a small
`ExampleXrMirrorPanel` (`examples/example_renderer.hpp`) that draws the live eye
images using these targets.

## XR cameras (scene-driven)

XR rendering is driven by the **scene's cameras**, not by a global switch. The
mechanism is a component plus an engine subsystem:

- `XRViewComponent`
  (`source/vultra/include/vultra/function/world/components/xr_view_component.hpp`)
  is attached to a camera entity to mark it as an XR view. Fields: `enabled`,
  `trackingOrigin` (`eLocal` / `eStage`), `stereoGraphMode`
  (`eSingleGraphStereo`), and `fallbackMono`. The camera entity acts as the XR
  rig / tracking origin; the runtime head/eye poses are applied **relative to
  that entity's transform**.

- `XRRuntimeSystem`
  (`source/vultra/src/function/openxr/xr_runtime_system.cpp`) is an
  `EngineSubsystem` that, when `config.render.xr.autoStartSessionFromScene` is
  set, inspects the world each frame and calls
  `IRenderBackendService::requestXRSession(...)`. It requests a session when the
  highest-priority active **primary** camera carries an enabled `XRViewComponent`
  (or, if there is no primary camera, when any active camera with an enabled
  `XRViewComponent` exists). Setting `config.render.xr.runtimeCameraOverride`
  forces the session on unconditionally.

Each frame, when XR is active, the render backend acquires the headset swapchain
image and publishes one `XREyeView` per eye via `xrEyeViews()`. An `XREyeView`
carries the per-eye `view` / `projection` / `pose` matrices, FOV, IPD, head and
eye position/rotation, the predicted display time, validity/tracking flags, and
the render targets (`target`, `stereoTarget`, `mirrorTarget`).

The camera system (`source/vultra/src/function/camera/camera_system.cpp`)
consumes those eye views: for an XR-enabled camera it clones the base
`RenderCamera` once per eye via `makeXREyeCamera(...)`, composing the eye pose
with the rig's world transform, and tags the result (`isXRView`, `viewIndex`,
`viewCount = 2`, etc.). Eye poses are only used when both position and
orientation are valid (`xrPoseUsable`); `fallbackMono` exists for the case where
a usable pose is not yet available.

## Stereo rendering

The headset swapchain is created as a **stereo (array) target**. `XRHeadset`
exposes a `StereoRenderTargetView` with three views over the same swapchain
image: `stereo` (the 2-layer array), `left`, and `right`
(`source/vultra/include/vultra/function/openxr/xr_headset.hpp`). This supports a
single-graph stereo path (`XRStereoGraphMode::eSingleGraphStereo`) where one
render graph renders both eyes — Vulkan **multiview** is used so the two eyes are
produced in one pass. The OpenXR triangle example shows the contract directly: it
builds a multiview pipeline with `setViewMask(0x3u)` and a fallback single-view
pipeline, and at draw time picks between them based on
`ctx.view().enableMultiview`, rendering with `layers = 2, viewMask = 0x3` for the
stereo case.

The declarative renderer drives per-eye rendering through the normal render-graph
machinery; see [`render_graphs.md`](render_graphs.md) for how `.vrg.json` graphs
are authored and [`scripted_render_passes.md`](scripted_render_passes.md) for
Lua `setup`/`execute` passes.

### XR scripted warp pass

`resources/render/passes/xr_custom_warp.lua` is a shipped example of a **scripted
project render-graph pass** for XR view synthesis (a "custom WARP backend"). Its
contract is:

- inputs: `source` (scene color), `depth` (scene depth)
- output: `color` (warped color, with **alpha = validity**)

It is a fullscreen pass (no geometry shader), so it also runs where the built-in
geometry warp is unavailable, and it exposes `disparityScale` and
`warpDirection` parameters reflected from its shader's `[properties]` block. Wire
it into a synthesis graph in place of the built-in warp node and feed its output
into an inpaint pass. See [`scripted_render_passes.md`](scripted_render_passes.md)
for the scripted-pass API it uses.

## View synthesis

View-synthesis (stereo reprojection) passes ship in the engine source under
`source/vultra/src/function/rendering/srp/builtin/passes/`:

- `geometry_warp_pass.cpp` — a `GeometryWarp` pass that reprojects a source view
  to a target view using a per-pixel/grid mesh, classifying over-stretched
  primitives as disocclusion holes.
- `pullpush_inpaint_pass.cpp` — a `PullPushInpaint` pass that fills the holes,
  with an optional depth-aware mode.

Their shared parameters live in `view_synthesis_settings.hpp`
(`ViewSynthesisSettings`: `sourceView`/`targetView`, `gridSize`,
`sideLenThreshold`, `useDepthAware`, `depthThreshold`). As the header notes,
these passes are written as a general source-to-target reprojection — the most
common use being synthesizing one stereo eye from the other — and the scripted
`xr_custom_warp.lua` above is the project-authored counterpart to the built-in
warp.

## Examples

Three runnable OpenXR examples live under `examples/openxr/` (each is a
`DemoAppHost` app that requests the `eXR` render-device feature flag):

| Directory | Target | Run |
| --- | --- | --- |
| `examples/openxr/triangle` | `example-openxr-triangle` | `xmake run example-openxr-triangle` |
| `examples/openxr/sponza` | `example-openxr-sponza` | `xmake run example-openxr-sponza` |
| `examples/openxr/gaussian_splatting` | `example-openxr-gaussian-splatting` | `xmake run example-openxr-gaussian-splatting` |

The triangle example is the smallest reference: it shows the multiview vs.
single-view pipeline selection, an ImGui panel that prints the active OpenXR
runtime name/version and the XR-enabled / mirror state, and the
`ExampleXrMirrorPanel` mirror display. Sponza and Gaussian-splatting are the
scene-content examples.

If no headset/runtime is available, these examples still launch and render flat
because of the graceful XR fallback described above.

## Areas to expand

The following are part of the subsystem but are intentionally not documented in
detail here, because the precise contract is best read from the source as it
continues to evolve:

- **XR input / actions** (`xr_input.cpp`, `xr_input_profile.cpp`,
  `xr_common_action.cpp`) — controller/action bindings.
- **Eye tracking** (`ext/xr_eyetracker.cpp`).
- The full **view-synthesis render-graph wiring** (how `GeometryWarp` /
  `PullPushInpaint` / scripted warp nodes are composed into a `.vrg.json`
  synthesis graph). The alpha-validity convention is described in
  `xr_custom_warp.lua` and its shader.
- **Tracking-origin (`local` vs `stage`) and stage/room-scale** specifics.
