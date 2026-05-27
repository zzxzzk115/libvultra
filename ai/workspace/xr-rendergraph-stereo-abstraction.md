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
- Declarative fullscreen project passes now derive their transient output
  texture shape from the input resource, preserving stereo array layers and
  `viewMask` for single-graph stereo chains.
- Declarative fullscreen project pass pipeline caches now include `viewMask`
  and configure multiview pipelines when the target framebuffer is multiview.
- Builtin camera-sized render target passes now preserve single-graph stereo
  texture shape and build per-`viewMask` graphics pipeline variants:
  `CompatibilityBaseColor`, `DirectGBuffer`, `DepthPre`, `VisibilityBuffer`,
  `ThinGBuffer`, `DeferredLighting`, `SSAO`, `SSR`, `SSRComposite`,
  `ToneMapping`, `FXAA`, `SelectionOutline`, and `Skybox`.
- Reserved declarative render graph resource names for future stereo warping
  and inpainting outputs: `stereo_warped_color` and
  `stereo_inpainted_color`.
- Added shader-side view context helpers in `gpu_scene.glsl`:
  `vultra_view_index()`, `vultra_eye_index()`, `vultra_view_count()`, and
  `vultra_is_stereo_view()`.
- Material graph generated shaders now use `MaterialGraphContext` internally
  while preserving the old function signature as a wrapper.
- Material graph node registry now exposes view/eye inputs so graph authors can
  branch or parameterize materials by view context without depending on raw
  shader built-ins.
- `vrendergraph` was upgraded to `v0.3.0` with schema `version`, resource
  descriptors, `ResourceRef` selectors, `when`, and `viewMode`; v0.2 string
  resource refs remain compatible.
- `xmake-repo` was updated so libvultra can consume `vrendergraph v0.3.0`.

## Verification

- `xmake build -y vultra-app` passed.
- `git diff --check` passed.
- `vrendergraph` examples passed for both
  `examples/deferred_virtual/scene.v02.json` and `scene.v03.json`.
- `xmake l scripts/test.lua --shallow -f debug=false,runtimes="MD" vrendergraph`
  passed in `../xmake-repo`.

## Follow-Up

- Declarative render graph resources should grow first-class stereo/multiview
  authoring fields instead of inferring only from `RenderView`.
- `vrendergraph v0.2.1` only stores resource names, so first-class resource
  descriptors require a dependency/schema update before `.vrg.json` can carry
  stereo resource metadata directly.
- Fullscreen declarative passes still need explicit shader keywords such as
  `VULTRA_MULTIVIEW` / `VULTRA_VIEW_COUNT` when the shader source actually
  consumes per-view built-ins.
- Screen-space shader code still needs explicit stereo sampling conventions
  for passes that read previous camera-sized textures. The graph now preserves
  layers/view masks, and shared view context helpers exist, but shader variants
  must decide whether to sample the current view layer, a stereo texture array,
  or a mono fallback.
- Future stereo warping and inpainting passes should use the new view/layer
  helpers and framegraph texture `viewMask` rather than pass-local constants.
