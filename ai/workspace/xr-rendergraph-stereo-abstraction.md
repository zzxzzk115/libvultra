# XR Render Graph Stereo Abstraction

Date: 2026-05-27

## Context

The RHI and OpenXR layers already expose the core stereo/multiview pieces:

- `rhi::FramebufferInfo` has `layers` and `viewMask`.
- Vulkan dynamic rendering forwards `viewMask`.
- `RenderView` carries multiview state and stereo cameras.
- OpenXR exposes a stereo swapchain texture plus per-eye views.

The render graph path still had several pass-local assumptions, including
manual `viewMask = 0x3u` setup and imported backbuffers without stereo intent.

## Changes

- Added `RenderView::renderTargetViewMask()`, `renderTargetLayerCount()`, and
  `usesSingleGraphStereo()`.
- Added `FrameGraphTexture::Desc::viewMask`.
- Frame graph texture attachment setup now propagates `Desc::viewMask` into
  `rhi::FramebufferInfo`.
- `framegraph::importTexture()` accepts an optional `viewMask`, used by render
  graph backbuffer imports.
- Gaussian splat stereo intermediate targets now carry `viewMask` in their
  framegraph texture descriptors.
- Multiview pipelines in final composition and Gaussian splat passes now use
  the actual `viewMask` instead of hard-coded `0x3u`.

## Verification

- `xmake build -y vultra-app` passed.

## Follow-Up

- Declarative render graph resources should grow first-class stereo/multiview
  authoring fields instead of inferring only from `RenderView`.
- Fullscreen declarative passes still need shader/pipeline multiview variants
  before they are truly single-graph stereo capable.
- Future stereo warping and inpainting passes should use the new view/layer
  helpers and framegraph texture `viewMask` rather than pass-local constants.
