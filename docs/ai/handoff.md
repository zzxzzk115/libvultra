# Handoff

Dated snapshot: 2026-11-11, branch `dev-slim`. Verify claims against the code before relying on them.

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
- `--libvultra_vri_dir=<checkout>` to build VRI from a local source tree without patches.
- `ShaderCompileOptions::debugInfo` and `optimize`: shader debug information and SPIRV-Tools optimization per
  compile instead of per C++ build mode.
- `LightBuffer`: the frame's lights as one storage buffer (`scene.lights`), read by `ForwardPath`.
- Import caches made through an `AssetSource` name files relative to its root and survive moving it.

## VRI

The default build still uses the pinned archive `d0c78964` plus the patches in `external/vri/`. Those patches,
and the Metal patches previously carried by a downstream fork, were rebased onto VRI `master` as four branches
(`fix/vulkan-correctness`, `fix/web-backends`, `fix/metal-backend`, `build/platform-archs`) and are open as
pull requests 26 to 29 of [zzxzzk115/VRI](https://github.com/zzxzzk115/VRI/pulls); none is merged. The Vulkan
branch was built and tested through this library; the Web, Metal and Android branches were not built on the
development machine and rely on VRI's own CI. Repin and delete `external/vri/*.patch` only after a VRI release
contains all four.

## Verification

Windows 11, MSVC 2022, release, RTX 4090 Laptop (Vulkan, validation enabled), VRI built from the local checkout
with all four branches merged:

- `xmake build -y --all` and `python tests/build_modules.py` (each module builds alone).
- Tests: render path (OpenPBR reference, material textures, live transforms, a second custom path), graph
  resources, graph definition, graph async (graphics and compute queues, parity with the single-queue plan),
  shader program (attributes, cooking, watched reload), shader compiler, file watcher, ray, asset pipeline,
  asset formats, glTF materials, debug draw, meshlets, device, device selection, window, desktop, display,
  GUI offscreen/lifetime/settings, VPK memory, image, input map.
- `example-offline`, `example-ui` and the window tests ran for a finite number of frames.

Not verified: Linux, macOS, Android, Web and any XR runtime or headset. `tests/platform/` is a separate project
that was edited by inspection and not built. The interactive feel of windows, hot reload and HiDPI was not
judged by a person.

## Known gaps

See [roadmap](../roadmap.md). In short: standard scene resources beyond HDR and depth, subresource support in
the graph, profiling across batches, an optimized builtin shader pack, XR controller input and mesh LOD.

## Local state

The uncommitted shadow receiver-culling work that existed on `dev-VRI` is kept as a git stash on the original
machine ("shadow receiver culling WIP"). It targets the removed renderer and is not part of this branch.
