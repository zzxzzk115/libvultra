# XR View Synthesis Render Graph

Date: 2026-05-28

## Intent

Vultra XR rendering uses two graph shapes:

- Normal render graphs implicitly compose to the current render target. In XR
  single-graph stereo this target is the stereo swapchain image, and passes must
  preserve multiview texture shape and `viewMask`.
- XR view synthesis graphs explicitly compose to the left or right eye
  backbuffer. They are reserved for stereo synthesis chains assembled from small
  atomic passes.

The old graph-facing `XrViewSynthesis` wrapper pass is removed from the public
render graph registry. Future synthesis work must not reintroduce one large
backend-selecting pass.

## Backbuffer Selection

Schema v0.3 is sufficient. Vultra interprets existing `ResourceRef.selector`
data for final composition outputs:

- no selector, `target`, or `backbuffer`: compose to the current render target;
- `left_backbuffer`, or `{ "resource": "backbuffer", "view": "left" }`:
  compose to the left XR eye target;
- `right_backbuffer`, or `{ "resource": "backbuffer", "view": "right" }`:
  compose to the right XR eye target.

If an explicit eye target is requested when XR targets are unavailable, the
renderer logs a clear error and skips that final pass.

`resources/render/xr_view_synthesis.vrg.json` is a reserved design graph until
atomic synthesis passes exist. Its composition passes must stay disabled so
selecting it cannot execute placeholder resources.

## Atomic Pass Direction

Synthesis graphs are built from atomic passes:

- `XrGeometryWarp`: geometry-shader-based warping from source color/depth to
  target-view warped color, with disocclusion-hole detection and depth-aware
  validity encoded in alpha.
- `XrPullPushInpaint`: pull-push repair of invalid/hole regions, depth-aware by
  default (non-depth-aware available via `useDepthAware = false`).

> Update (2026-06): the geometry warp was upgraded from the placeholder
> `warpStrength` disparity approximation to a real geometry-shader port (matrix-
> driven reprojection + stretched-triangle hole detection), and pull-push gained
> a depth-aware mode. Requires a geometry-shader-capable shader profile; see the
> WebGPU note below.

## Backend Contract (Lua-extensible)

A warp/inpaint "backend" is just an atomic render-graph pass with a fixed I/O
contract. Users extend synthesis by authoring a scripted (`.lua`) pass that
honours the contract and wiring it into a `.vrg.json` graph in place of the
built-in `XrGeometryWarp` / `XrPullPushInpaint` nodes (the spec forbids one large
backend-selecting pass, so backends are graph nodes, selected by node type).

- Warp backend: `inputs = { "source", "depth" }`, `outputs = { "color" }`.
- Inpaint backend: `inputs = { "source" }`, `outputs = { "color" }`.
- Scripted passes bind declared inputs to fragment/compute `set = 3` bindings in
  declaration order (see the `vultra-lua-render-pass` skill).

**Alpha-validity convention** (shared by warp output and inpaint input):

- `[0, 0.5]`  valid    — `depth = alpha * 2`
- `(0.5, 1)`  invalid  — `depth = (alpha - 0.5) * 2` (warped but unreliable)
- `1.0`       hole     — no data (uncovered; comes from the attachment clear)

In non-depth-aware mode this collapses to `alpha = 0` (valid) / `alpha = 1`
(hole). A custom warp must clear its color target to `alpha = 1` so uncovered
pixels read as holes for the inpaint stage. See `resources/render/passes/`
for an example custom warp backend.

## Reprojection (stereo + temporal)

The geometry warp is matrix-driven, not eye-specific. Its `XrWarpBlock` UBO
(set 1, binding 0) carries an unproject + project pair:

- `sourceInvViewProj` — source NDC+depth → world (homogeneous), one perspective
  divide applied in the shader for accuracy;
- `targetViewProj[2]` — world → target layer NDC (per multiview layer).

Stereo fills these from `RenderView.multiviewCameras` (the source eye's inverse
view-projection + each eye's view-projection); when the source layer equals the
target layer the matrices are mutually inverse, so the source eye passes through.
Without stereo cameras (mono/preview) the warp is an identity pass-through.

**Enlarged source / warp modes (not implemented):** LeftToRight / RightToLeft /
CenterToLeftRight with an enlarged (or union-FOV) source — including the
"warp-back" of the source eye to its own display FOV — are designed as a
first-class synthesis plan (rendered SOURCE views + synthesized TARGET views) in
`ai/specs/xr-view-synthesis-warp-modes.md`. That is the substantive remaining
engine work; the current graph only renders both eyes and warps one.

**Temporal extension point (not implemented):** a temporal backend is the same
warp pass fed different matrices and a previous-frame source — set
`sourceInvViewProj` from the previous frame and `targetViewProj` from the current
frame (or vice versa), and supply the previous frame's color/depth as `source`/
`depth`. The graph-node contract is already shape-compatible. What temporal still
needs (and stereo does not): per-frame history textures for previous color/depth
and storage of the previous-frame camera matrices on `RenderView` / frame
resources. Until that plumbing exists, only the stereo warp is wired.

## WebGPU / Compatibility Profile

Geometry shaders are unsupported on WebGPU; the `vshadersystem` toolchain
warn-skips the geometry stage for the WebGPU target, so the geometry-warp
backend is unavailable on the compatibility/WebGPU profile (the pipeline fails to
build and the pass logs an error). WebGPU graphs should select a non-geometry
warp backend (e.g. a custom Lua/compute backend) instead.

## Multiview Contract

Normal graph passes must be multiview-first:

- camera-sized transient outputs inherit input layers and `viewMask`;
- graphics pipelines are keyed by framebuffer `viewMask`;
- fullscreen project passes that read multiview inputs must preserve multiview
  output shape or warn.

This lets the default XR graph render through the normal chain without manually
declaring eye-specific backbuffers.
