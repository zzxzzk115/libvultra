# XR render graph view synthesis redesign

Date: 2026-05-28

## Changes

- Removed the graph-facing `XrViewSynthesis` pass from the declarative renderer
  registry and Render Graph editor catalog.
- Added XR eye target pointers to `RenderView`, filled from active OpenXR eye
  views during render system view construction.
- Extended declarative final composition target import so graph output refs can
  select `left_backbuffer` or `right_backbuffer` through schema v0.3 selectors.
- Updated `resources/render/default_xr.vrg.json` to use the normal XR graph
  path without synthesis.
- Added `resources/render/xr_view_synthesis.vrg.json` as a reserved design
  graph that explicitly composes left and right eye targets. Placeholder passes
  are disabled until atomic synthesis producers exist.
- Updated the XR view synthesis spec and task to document the atomic-pass
  direction.
- Added an XR session transition barrier in `RenderBackendSystem`: starting or
  releasing the OpenXR backend now waits for the render device to idle, clears
  cached eye/mirror targets, and skips that render frame. This keeps XR
  swapchain creation/destruction out of the same frame as renderer/camera
  edits from the editor UI.
- Added Scene View render-target resize debouncing as an editor UX fix: the
  existing target remains active while the docked view is being dragged, and a
  replacement target is created only after the requested size is stable.
- Tightened editor VRAM usage around Game View/XR mirror:
  - Game View now distinguishes visible UI size from an actually available
    offscreen render target.
  - XR mirror preview no longer allocates a Game View offscreen target and no
    longer publishes the dock window width as a render target size.
  - Render Graph runtime preview uses the real Game View target extent only
    when such a target exists; otherwise it falls back to a compact 640x360
    preview target.
  - Game View render-target resize now uses the same short stability debounce as
    Scene View, so drag-resizing does not continuously allocate large transient
    render targets.
- Added a lightweight RHI memory resource ledger and Profiler Memory tab:
  - Texture and buffer allocations register size, type, memory kind, label, and
    details with `RenderDeviceMemoryTracker`.
  - FrameGraph transient resources are relabeled as FrameGraph Texture/Buffer
    entries with their descriptor details.
  - Asset textures are relabeled with their resolved asset URI or UUID.
  - The Profiler window now has a `Memory` tab that sorts tracked resources by
    size and summarizes texture vs buffer bytes.
- Reduced high-resolution XR VRAM pressure exposed by the Memory tab:
  - Default shadow resolution is now 2048 instead of 4096, reducing the default
    4-cascade shadow atlas from 8192x8192 Depth32F (~256 MiB) to 4096x4096
    Depth32F (~64 MiB).
  - FrameGraph transient resource reuse now follows the default two
    frames-in-flight window (`minReusable=2`, `maxCached=4`) instead of keeping
    four to ten frames of same-sized large textures resident.

## Notes

- The old low-level C++/shader implementation files for view synthesis remain
  in the tree for now, but no runtime/editor graph entry points reference them.
- Explicit eye final composition requires active XR eye targets. If unavailable,
  the renderer logs once and skips the requested final pass.
- A broader Scene/Game View resize debounce experiment was reverted after
  runtime testing showed the renderer-key crash was not caused by editor view
  resizing. The narrower Scene View debounce was reintroduced only to reduce
  resize flicker.
- The horizontal Game View XR resize crash path was likely caused by editor
  preview consumers treating the mirror panel width as a real render target
  extent. The current fix keeps mirror display and render allocation separate.
- For high-resolution XR devices such as Pimax Crystal, expected memory
  pressure should be inspected with the new Profiler Memory tab before tuning:
  multiview FrameGraph render targets scale as width * height * layers * bytes
  per pixel per pass, so RGBA16F/depth G-buffer chains can dominate VRAM at
  dual-eye 4K-class extents.
- The first Sponza/Pimax Memory-tab inspection showed that the dominant VRAM
  consumers were FrameGraph transients, especially repeated 8192x8192
  Depth32F shadow atlases and dual-layer XR G-buffer/fullscreen textures, not
  asset textures.

## Verification

- `Get-Content ... -Raw | ConvertFrom-Json | Out-Null` passed for:
  - `resources/render/default.vrg.json`
  - `resources/render/default_xr.vrg.json`
  - `resources/render/xr_view_synthesis.vrg.json`
- `xmake build -y vultra-app` passed.
- After the Game View VRAM tightening, `xmake build -y vultra-app` passed again
  and `git diff --check` was clean.
- After adding RHI memory resource tracking and the Profiler Memory tab,
  `xmake build -y vultra-app` passed again and `git diff --check` was clean.
- After reducing shadow resolution and transient cache retention,
  `ConvertFrom-Json` passed for the render graph assets, `xmake build -y
  vultra-app` passed, and `git diff --check` was clean.
