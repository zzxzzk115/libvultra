# Render Graph, Thumbnail, and New Project Fixes

Date: 2026-05-30

## Changes

- Fixed model-root/offline asset thumbnails by updating the preview world's
  `TransformComponent::worldMatrix` before bounds/camera setup and again when
  delayed assets become ready. This keeps imported model child/submesh
  transforms from rendering at identity in the thumbnail pass.
- Fixed empty scene thumbnails getting stuck in the loading screen. Scene
  thumbnails may now proceed even when the scene has no mesh/splat bounds.
- Bumped the rendered thumbnail cache version to regenerate stale model, mesh,
  scene, and material graph thumbnails.
- Updated declarative render graph runtime project-pass discovery to read
  Lua assets from arbitrary project paths, both from filesystem sources and the
  asset registry/VPK path. Files are only treated as graph passes when they
  contain `RenderGraphPass` and return a valid pass table.
- Updated the Render Graph editor pass catalog and shader-ref scan to use the
  same arbitrary-path Lua pass discovery instead of only `render/passes/`.
- Added a default `resources/materials/default.vmatgraph.json` to new projects
  created by the launcher so the editor does not open a missing material graph
  after project creation.
- Fixed local XR/stereo preview targets so the render view and framebuffer both
  use the same two-layer/view-mask setup. This addresses XR graph preview and
  synthesis paths that render into editor-local stereo targets rather than
  OpenXR eye swapchain targets.
- Split project Lua render graph pass parsing by pipeline type. Custom passes
  now infer or accept `pipeline = "compute"` when `shader.compute` is provided,
  create a compute pipeline from the selected shader library, bind declared
  texture inputs plus a storage output, and dispatch either explicit group
  counts or output-size-derived groups. Ray tracing pass declarations are now
  recognized separately instead of being mistaken for fullscreen surface
  shaders; execution still needs script-facing TLAS/SBT binding support.
- Added project shader workflow templates and a compute shader example. Project
  shader manifests now include `**/*.vshader` so shader sources are not limited
  to fullscreen/material-graph folders.
- Added the missing `Invert` project render graph pass Lua descriptor for the
  compute shader example, and made the Render Graph editor shader-ref metadata
  compute-aware so compute project passes appear with their shader label.
- Added Content Browser creation templates for Render Graph render,
  post-processing, compute, and raytracing passes. The create popup now scans
  project `.vshader` files by stage and writes the selected shader binding into
  the generated pass Lua.
- Added an Inspector drawer for `RenderGraphPass` Lua source assets. Pass files
  can now edit type, pipeline, ports, shader bindings, and compute dispatch
  mode without opening the Lua source.
- Updated the RenderGraphPass Inspector drawer to use stage-aware shader
  selectors for vertex, fragment, compute, and ray tracing shader fields, with
  manual text input kept as an advanced fallback.
- Moved material graph generated shaders out of `resources/` and into the
  project-local `.vultra/generated/shaders/` tree. vasset now implicitly
  compiles those generated sources into the same project shader library with a
  `generated/` virtual prefix, without exposing any generated-root settings in
  the user-authored shader manifest.
- Stopped copying the fullscreen triangle vertex shader into new projects.
  Fullscreen project passes can now use `vertexLibrary = "builtin"` with a
  project fragment shader, so project content owns only the effect shader.

## Verification

- `git diff --check` passed with only existing CRLF warnings for unrelated
  dirty generated/resource files.
- `resources/materials/default.vmatgraph.json`,
  `resources/render/default.vrg.json`, and
  `resources/render/xr_view_synthesis.vrg.json` parsed via `ConvertFrom-Json`.
- `xmake build -y vultra-app` passed.
- Re-ran `resources/render/xr_view_synthesis.vrg.json` JSON parse and
  `xmake build -y vultra-app` after the empty-scene thumbnail, arbitrary Lua
  pass discovery, and local stereo framebuffer fixes; passed.
- Re-ran `git diff --check` and `xmake build -y vultra-app` after custom
  compute project-pass support; passed.
- Compiled `resources/shaders/compute/invert.comp.vshader` for Vulkan and
  WebGPU profiles with `vshaderc compile`; passed. Built a Vulkan project
  shader library containing fullscreen, material-graph, and compute shaders
  with `vshaderc build -I builtin/shaders`; passed.
- Re-ran `git diff --check` and `xmake build -y vultra-app` after shader
  templates and new-project shader manifest updates; passed.
- Re-ran `git diff --check` and `xmake build -y vultra-app` after adding the
  `Invert` project pass descriptor and editor compute shader metadata; passed.
- Re-ran `git diff --check` and `xmake build -y vultra-app` after adding
  Content Browser pass creation with shader selection; passed.
- Re-ran `git diff --check` and `xmake build -y vultra-app` after adding the
  RenderGraphPass Inspector drawer; passed.
- Re-ran `git diff --check` and `xmake build -y vultra-app` after adding
  shader selectors to the pass drawer; passed.
- Re-ran `git diff --check` after moving generated shader output under
  `.vultra`; passed with only the existing CRLF warnings. `xmake build -y
  vasset-import` and `xmake build -y vultra-app` both passed.
- Re-ran `git diff --check` and `xmake build -y vultra-app` after switching
  fullscreen project-pass templates to the builtin fullscreen triangle; passed.
- Re-ran `git diff --check` and `xmake build -y vultra-app` after making the
  generated shader root implicit; passed.

## Follow-Up

- Runtime `Pixelate` disable should be rechecked in a packaged project now that
  the pass type registers from arbitrary Lua assets in VPK; if it still crashes,
  inspect the active graph passthrough rewrite and resource refs emitted for
  disabled project passes.
- XR view synthesis still needs headset/runtime testing around explicit XR eye
  backbuffers; the editor-local stereo target mismatch was fixed in this slice.
- Material graph flexibility and profiler hot spots are broader design/perf
  tasks and were not changed in this slice.
- Script-defined ray tracing passes need a design for binding acceleration
  structures, shader binding tables, and payload conventions before they can be
  safely executed from arbitrary Lua pass declarations.
- WebGPU project-library verification still hits the existing generated
  material-graph bindless texture conversion issue; the new compute example
  itself compiles under WebGPU with UBO material mode.
