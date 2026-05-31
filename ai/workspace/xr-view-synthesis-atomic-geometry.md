# XR View Synthesis Atomic Geometry Handoff

Date: 2026-05-29

## Changes

- Replaced the default XR view synthesis warping backend with fixed-grid
  geometry-based warping (`geometry`) and removed the previous adaptive-mesh
  implementation from the C++ pass.
- Added lightweight `IXrWarpingBackend` and `IXrInpaintingBackend` interfaces;
  the default implementations are `XrGeometryWarpPass` and
  `XrPullPushInpaintPass`.
- Added `XrGeometryWarp` and `XrPullPushInpaint` as render graph atomic pass
  types.
- Added builtin geometry-warp vertex/fragment shaders. The warp pass draws a
  generated grid directly from `gl_VertexIndex`, avoiding CPU mesh/index buffer
  uploads and the previous compute-generated adaptive mesh.
- Kept PullPush non-depth-aware. Alpha is used only as validity: warped pixels
  write alpha 1, holes clear to alpha 0.
- Updated `resources/render/xr_view_synthesis.vrg.json` to document the atomic
  chain while keeping all synthesis placeholder passes disabled.
- Converted `resources/render/xr_view_synthesis.vrg.json` from a reserved
  external-input fragment into an executable render graph. It now renders the
  scene with the normal DirectGBuffer/deferred path, composes the left eye
  directly, and synthesizes/composes the right eye through geometry warp plus
  pull-push.
- Added render graph condition tokens for `xr_eye_targets` and XR preview
  detection. The XR synthesis graph now uses explicit left/right backbuffers
  only when eye targets exist, and falls back to normal final composition for
  preview paths without XR eye targets.
- Fixed the reserved graph editor layout so imported resource nodes are placed
  with `resource:<name>` metadata keys, and right-eye composition now connects
  directly from `RepairRightEye.color`.
- Fixed the Render Graph editor canvas to actually draw resource nodes. Before
  this, resource pins existed only as link endpoints, so links from imported
  resources appeared to originate off-canvas even when metadata positions were
  present.
- Filtered editor resource nodes to resources referenced by pass inputs. This
  keeps output-only backbuffer selector resources from appearing as dangling
  imported source nodes.
- Updated the runtime frame graph texture preview for XR/stereo graphs. When a
  selected debug texture has layer 0/1 captures for the same resource, the
  popup now shows the left and right eyes side by side and applies preview
  controls to both layers.
- Fixed XR synthesis graph activation. Explicit left/right backbuffer output
  passes now require both `single_graph_stereo` and `xr_eye_targets`; geometry
  warp/repair only run in `single_graph_stereo`; mono/per-eye fallback paths use
  ordinary final composition. Backbuffer selector resources are no longer
  declared as unconditional imported resources.
- Added editor-local stereo preview support for XR render graphs. The graph
  preview camera now renders XR graphs into a local two-layer target, debug
  capture splits the layers, and the overlay displays the left/right layers
  side by side.
- Removed unused adaptive-mesh shader sources so they are no longer compiled
  into builtin shader libraries.

## Verification

- `resources/render/xr_view_synthesis.vrg.json` parsed via `ConvertFrom-Json`.
- `git diff --check` passed.
- `xmake build -y vultra-app` passed after adding the atomic pass implementation
  and shaders.
- `xmake build -y vultra-app` passed again after removing obsolete adaptive
  shader sources.
- `xmake build -y vultra-app` passed after adding the explicit backend
  interfaces.
- `resources/render/xr_view_synthesis.vrg.json` parsed and
  `xmake build -y vultra-app` passed after the graph layout/connectivity fix.
- `resources/render/xr_view_synthesis.vrg.json` parsed and
  `xmake build -y vultra-app` passed after making the graph executable.
- `resources/render/xr_view_synthesis.vrg.json` parsed and
  `xmake build -y vultra-app` passed after adding the preview-compatible
  composition fallback.
- `xmake build -y vultra-app` passed after adding resource-node rendering to
  the editor canvas.
- `xmake build -y vultra-app` passed after filtering output-only resource nodes
  from the editor canvas.
- `resources/render/xr_view_synthesis.vrg.json` parsed,
  `git diff --check` passed, and `xmake build -y vultra-app` passed after
  adding XR/stereo side-by-side runtime texture preview.
- `resources/render/xr_view_synthesis.vrg.json` parsed,
  `git diff --check` passed, and `xmake build -y vultra-app` passed after
  fixing XR graph conditions, removing unconditional eye backbuffer imports,
  and adding local stereo graph preview.

## Notes

- The geometry warp currently uses the existing depth-disparity approximation
  and `warpStrength` parameter. It does not yet consume per-eye projection/view
  matrices from a dedicated XR warp uniform.
- `XrViewSynthesisPass` remains as an internal compatibility/composition helper,
  but the public render graph path is now the atomic `XrGeometryWarp` plus
  `XrPullPushInpaint` chain.
