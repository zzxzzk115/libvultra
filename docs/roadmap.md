# Roadmap

libvultra stays a small, readable graphics kernel. [architecture.md](architecture.md) describes what exists;
[BUILD.md](../BUILD.md) documents targets and verification. Projects, packages, scripting, the command line host
and editors are built on top of it in other repositories and are not planned here.

All serialized-data and file-format versions remain **1** before a release, with deliberate breaking changes.

## Open work

| Area | What is missing | Acceptance |
| --- | --- | --- |
| Scene resources | Render paths provide only `scene.hdr` and `scene.depth`. Normals, albedo, motion vectors, camera jitter and previous-frame matrices have no standard names yet. | Named, documented resources with format and convention, provided by `ForwardPath` and checked by `validateSceneResources()`. |
| Render graph resources | Textures are single-mip, single-layer 2D with whole-resource state. | Mip chains, arrays and cubes with matching views, initialization and per-subresource barriers; regressions for each. |
| Async compute | One compute queue; the per-pass profiler covers single-batch plans only. | Profiling across batches; a regression with several compute batches in one frame. |
| Scene lights | `LightBuffer` holds at most 64 lights, and no test reads it from a graph pass. | A capacity decided by the first path that needs more, and a regression with a compute pass reading `scene.lights`. |
| Shader optimization | `ShaderCompileOptions::optimize` exists; the builtin shader pack is cooked without it. Cooking the pack optimized was measured on 2026-11-11 (forward path, one glTF model, 3840x2160, RTX 4090 Laptop, Vulkan): GPU frame time 0.717 ms against 0.702 ms, within run-to-run noise, with a 19% larger pack and eight HDR values one half-float step apart. | A scene or device on which optimization measurably lowers frame time; heavier shaders such as path tracing and meshlets were not measured. |
| XR input | The `xr` module exposes head pose and views, not controller poses or actions. | Controller poses and action state, verified on a runtime, before any package relies on them. |
| Mesh LOD | Meshlets are built; there is no LOD generation or selection. | An import setting that builds LOD chains, and a render path that selects them. |
| Platforms | Verified on Windows with Vulkan and, by the test suite, on macOS arm64 with Vulkan (MoltenVK) and Metal. On Android the probe and the examples ran on the x86_64 emulator and on one arm64 device; a rotated display needs a VRI fix that is not released yet. Linux and Web builds were not rebuilt after the module regrouping. | Build and run the examples on each claimed platform; state anything unverified. |

## Rules for new work

- A render technique is a `RenderPath` or a graph pass, not a change to `ForwardPath`.
- A new capability goes into the module that owns its responsibility. `xmake lua tests/build_modules.lua` must keep
  passing: a module may only use the modules it lists as dependencies.
- Rendering changes need finite-frame runs, validation-layer checks and image readback. Compilation or a
  successful exit code alone does not prove correct rendering.
