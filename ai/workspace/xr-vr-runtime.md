# XR VR Runtime Handoff

## Goal

First-stage XR runtime support focuses on stable VR rendering and stereo render
graph architecture. Stereo warping and inpainting are intentionally not
implemented in this slice.

## Implemented

- Added `XRViewComponent` as an additive scene component for camera entities.
- Exposed XR view fields through scene reflection, scene serialization, and the
  Inspector add-component flow.
- Added readable Inspector controls for XR View and runtime OpenXR/mirror status.
- Vulkan startup now requests XR-capable device creation by default, matching a
  Unity-style "XR content can start later" workflow. `--no-xr` forces XR off;
  `--xr` remains as an explicit opt-in alias.
- OpenXR headset/session creation is lazy. Runtime builds let
  `XRRuntimeSystem` scan the active world for enabled XR camera views, request
  the backend session when needed, and release it when XR content is no longer
  present.
- Editor Scene View does not start OpenXR just because an `XRViewComponent`
  exists. Editor startup disables scene-driven XR auto-start; Game View is the
  explicit XR preview request source.
- Added `--xr`, `--no-xr`, `--xr-mirror`, and `--no-xr-mirror` launch options.
- Extended backend XR eye view data with pose, FOV, IPD, predicted display time,
  and tracking/validity flags.
- Camera cooking now composes the camera entity transform as the XR origin with
  each OpenXR local eye pose when `XRViewComponent.enabled` is true.
- Manual editor cameras only opt into XR when their `RenderCamera` explicitly
  requests XR, so Scene View and asset previews are not accidentally sent to the
  headset.
- `XRViewComponent.fallbackMono` lets camera entities render as normal mono
  cameras when XR pose data is unavailable.
- Added `StereoRenderMode` to `RenderView` and blackboard-visible
  `StereoViewData` for stereo graph consumers.
- Game View requests editor XR preview for a primary camera with
  `XRViewComponent` and displays the previous frame's left/right XR mirror
  textures when OpenXR is active. Packaged runtime sessions remain driven by
  `XRRuntimeSystem`.
- XR mirror previews use the same shader preview path as Frame Texture Debug
  when gamma correction is enabled, and gamma correction is on by default. Game
  View exposes the same shared `Gamma` toggle as the renderer mirror panel.
- Game View shows a clear `XR Disabled` state when the camera wants XR but
  OpenXR/runtime/headset initialization is unavailable or XR was forced off.
- Registered stereo and previous-frame resource names for future reprojection,
  warping, and inpainting graph passes.
- Added `resources/render/stereo_vr.vrg.json` to newly generated projects.

## Warping/Inpainting Extension Points

- Current-frame data is available through `StereoViewData` and
  `GPUStereoCameraBlock`.
- Graph resource names reserved for future passes:
  `stereo_color`, `stereo_depth`, `previous_stereo_color`,
  `previous_stereo_depth`, `previous_stereo_pose`, and
  `stereo_reprojection_metadata`.
- Runtime frame-history ownership is still future work; the current slice only
  reserves graph-facing keys and stereo metadata.

## Verification

- `xmake build -y vultra-app` passed.
- Re-ran `xmake build -y vultra-app` after adding XR launch options and Game
  View XR preview support; passed.
- Re-ran `xmake build -y vultra-app` after switching to default XR-capable
  Vulkan startup and lazy XR session requests; passed.
- Re-ran `xmake build -y vultra-app` after moving XR session requests from Game
  View to `XRRuntimeSystem`; passed.
- Re-ran `xmake build -y vultra-app` after enabling default XR mirror gamma and
  adding the Game View gamma toggle; passed.
- Re-ran `xmake build -y vultra-app` after splitting editor Game View XR preview
  requests from runtime scene auto-start; passed.

## How To Exercise In Editor

1. Launch with `vultra --editor --project <project-dir>`. Use `--no-xr` only
   when you want to force XR off.
2. Select the primary camera entity.
3. Add `XR View` in the Inspector, keep `Enabled` and `Fallback Mono` on.
4. Switch the editable render graph to `res://render/stereo_vr.vrg.json` if the
   project has the generated stereo template.
5. Scene View editing does not start an OpenXR session. Open Game View to start
   editor XR preview and inspect the left/right mirror views when mirror mode is
   enabled; otherwise Game View shows an explicit disabled/waiting state.

## Notes

- Existing mono render graphs and scenes remain compatible.
- General mesh passes are not yet fully multiview shader-aware, so the renderer
  still uses per-eye fallback unless the existing multiview path is supported by
  the active pass set.
