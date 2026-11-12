# Handoff

Dated snapshot: 2026-11-11, branch `dev-slim`. Verify claims against the code before relying on them.

This library is the kernel of a larger restructuring whose steps are called M0 to M5. They are described, with
their state and the steps to verify the latest one, in
[the milestones page of Vultra-CLI](https://github.com/zzxzzk115/Vultra-CLI/blob/main/docs/milestones.md). M0 and M1 are this
repository; from M2 on the work is in Vultra-CLI, Vultra-Packages and vultra-index, which build on it.

## Start here

```sh
git clone --branch dev-slim https://github.com/zzxzzk115/libvultra.git
cd libvultra
xmake f -m release -y
xmake build -y --all
xmake run example-offline --frames 3 --output build/.tmp/first-run
```

Read [README](../../README.md), [BUILD](../../BUILD.md), [architecture](../architecture.md) and
[roadmap](../roadmap.md). On Windows, run xmake from PowerShell or cmd: in Git Bash it detects the platform as
mingw.

## What changed on dev-slim

`dev-slim` branches from `dev-VRI` at `f9aa0575` and reduces the library to a kernel. `source/` went from about
38,000 to about 23,000 lines of C++.

Removed. The last commit that contains all of it is `dev-VRI` `f9aa0575`:

- The generated C ABI, the libclang code generator, the compute bridge and reflection annotations
  (`source/api`, `scripts/codegen*.py`).
- VGui and the RmlUi dependency.
- The `.vshader` language with its ANTLR parser, `ShaderAsset`, `MaterialInstance`, `ShaderMaterial` and
  `ShaderRuntime`.
- `BuiltinRenderer` with its deferred, shadow-cascade and mesh-shading stages.
- The GLFW window backend. SDL3 is the only desktop backend.
- The retained native GUI plan (documentation only; it was never implemented).

Added:

- Ten modules that are also the build features: core, platform, gpu, shader, graph, assets, render, ui, app, xr.
- `RenderPath` and `ForwardPath` (`vultra.forward`).
- Slang user-attribute and field-initializer reflection, stored in cooked programs.
- A render graph execution plan with batches per queue, async compute through `execute(Frame&)`, and
  `writeDot()`.
- `FileWatcher`, used by shader hot reload.
- Ray, plane, triangle and rectangle intersection helpers.
- `--libvultra_vri_dir=<checkout>` to build VRI from a local source tree.
- `ShaderCompileOptions::debugInfo` and `optimize`: shader debug information and SPIRV-Tools optimization per
  compile instead of per C++ build mode.
- `LightBuffer`: the frame's lights as one storage buffer (`scene.lights`), read by `ForwardPath`.
- Import caches made through an `AssetSource` name files relative to its root and survive moving it.
- Errors as values: every fallible operation returns `std::expected<T, vultra::Error>`, fallible types are made by
  static `create()`, app hooks and RenderGraph pass callbacks return `std::expected<void, Error>`, and
  unreachable states call `vultra::panic()`. Remaining `catch` blocks only convert third-party throws
  (argparse, nlohmann_json, spdlog, `std::filesystem` via FileWatch) where Vultra calls them. On macOS the
  OpenXR loader's hidden `std::exception`/`std::logic_error` typeinfos made base-class catches fail, which is why
  catches name the exact thrown type.

## VRI

The default build uses the tagged VRI v0.2.1 release through the shared xmake `vri` recipe; `external/vri/`
holds no patches. The former patches, and the Metal patches previously carried by a downstream fork, were
merged upstream as pull requests 26 to 29 of [zzxzzk115/VRI](https://github.com/zzxzzk115/VRI/pulls). The
Vulkan changes were built and tested through this library; the Web, Metal and Android changes were not built
on the development machine and rely on VRI's own CI. The Linux desktop CI job of the tagged commit did not
reach its build because the runner could not resolve its package mirror; it passed on each pull request.

v0.2.1 (pull request 32) adds the two fixes macOS needs, pull requests 30 (the Vulkan backend no longer assumes
a 1.3 device; MoltenVK reports 1.2) and 31 (Metal reports point and range timestamps separately). With v0.2.0,
Vulkan found no adapter on macOS and Metal apps failed when the profiler was created. The `vri` recipe in
zzxzzk115/xmake-repo carries v0.2.1 since its pull request 13.

## Verification

Windows 11, MSVC 2022, release, RTX 4090 Laptop (Vulkan, validation enabled), VRI built from the local checkout
with all four branches merged:

- `xmake build -y --all` and `xmake lua tests/build_modules.lua` (each module builds alone).
- Tests: render path (OpenPBR reference, material textures, live transforms, a second custom path), graph
  resources, graph definition, graph async (graphics and compute queues, parity with the single-queue plan),
  shader program (attributes, cooking, watched reload), shader compiler, file watcher, ray, asset pipeline,
  asset formats, glTF materials, debug draw, meshlets, device, device selection, window, desktop, display,
  GUI offscreen/lifetime/settings, VPK memory, image, input map.
- `example-offline`, `example-ui` and the window tests ran for a finite number of frames.

macOS 27 (Darwin 27.2), Xcode 27 SDK, Apple M2, release, the VRI v0.2.1 package: `xmake build -y --all` and every test target with `libvultra_graphics_api=vulkan` (MoltenVK from Vulkan
SDK 1.4.309, validation enabled) and `metal`. All pass except `test-meshlets` on Vulkan (MoltenVK has no mesh
shading); it passes on Metal. `example-basics vri` and `graph` and `example-ui` ran for a finite number of frames on both backends;
`tests/build_modules.lua` was not run, and clang-tidy
covered only the sources the macOS build compiles.

The macOS work found that ISPC 1.28.2 drops a masked `n--` in bc7e's block encoder, so every solid-color BC7
block was wrong on every ISA; `external/bc7enc` carries a two-line patch. Windows output changes accordingly and
was not re-verified.

Not verified: Linux, Android, Web and any XR runtime or headset. `tests/platform/` is a separate project
that was edited by inspection and not built. The interactive feel of windows, hot reload and HiDPI was not
judged by a person.

## Known gaps

See [roadmap](../roadmap.md). In short: standard scene resources beyond HDR and depth, subresource support in
the graph, profiling across batches, XR controller input and mesh LOD. Optimizing the builtin shader pack was measured
and not adopted; the roadmap records the numbers.

## Local state

The uncommitted shadow receiver-culling work that existed on `dev-VRI` is kept as a git stash on the original
machine ("shadow receiver culling WIP"). It targets the removed renderer and is not part of this branch.
