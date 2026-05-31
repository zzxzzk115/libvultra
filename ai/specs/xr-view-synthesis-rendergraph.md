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

Future synthesis graphs should be built from atomic passes:

- `XrGeometryWarp`: non-adaptive geometry-based warping from source color/depth
  to target-view warped color/validity.
- `XrPullPushInpaint`: non-depth-aware repair of invalid/hole regions.

Adaptive mesh warping is intentionally out of scope for the first atomic pass
version because the previous implementation caused stability risk.

## Multiview Contract

Normal graph passes must be multiview-first:

- camera-sized transient outputs inherit input layers and `viewMask`;
- graphics pipelines are keyed by framebuffer `viewMask`;
- fullscreen project passes that read multiview inputs must preserve multiview
  output shape or warn.

This lets the default XR graph render through the normal chain without manually
declaring eye-specific backbuffers.
