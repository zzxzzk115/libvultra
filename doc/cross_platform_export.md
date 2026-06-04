# Cross-platform export templates

> **Status (2026-06):**
> - **Desktop (Vulkan):** working.
> - **Web (WASM / WebGPU):** the whole pipeline is in place and the runtime **builds, links,
>   and runs in the browser** — but it does **not render correctly yet**. The engine reaches a
>   live render loop without crashing, then produces a **black frame**: on WebGPU the universal
>   renderer falls back to an experimental *compatibility* render graph
>   (`universal_compat.vrg.json`) whose output is currently empty. This is render-correctness
>   bringup for the WebGPU backend, tracked separately from the export pipeline. The
>   architecture (editor-free `vultra-runtime` player + runtime-fetch VPK + engine-only
>   template) is sound; what remains is per-pass WebGPU debugging (RenderDoc / instrumentation).
>   The same WebGPU black-frame issue reproduces on **desktop `--backend webgpu`**, confirming it
>   is a backend issue, not wasm-specific.
> - **Android:** design only (not implemented) — see the Android section.

The editor's **Export & Run** packages a project for a target platform. Every target reuses
the same two ideas:

1. **Pack the project into a `.vpk`** (read-only, zstd-compressed asset package) via the
   `vasset` CLI. This step is platform-agnostic — see `packProjectVpk` in
   [editor_app_build.cpp](../source/vultra_app/src/editor_app/editor_app_build.cpp).
2. **Combine the `.vpk` with a prebuilt "export template"** (a runtime that knows how to open a
   `.vpk`) to produce a distributable.

The runtime player used as the template source for *all* platforms is the editor-free
`vultra-runtime` target ([source/xmake.lua](../source/xmake.lua),
[runtime_main.cpp](../source/vultra_app/src/runtime/runtime_main.cpp)). It builds for desktop,
wasm and android and contains none of the editor / launcher / MCP / asset-import code.

The editor's export dispatcher (`runBuildAndLaunch`) switches on `BuildSettings.targetPlatform`:
`Windows`/`macOS`/`Linux` → `exportDesktop`; `WebGPU` → `exportWeb`; `Android` → planned.

## Desktop

`exportDesktop` packs `<Project>.vpk`, copies the runtime executable next to it, and launches
`vultra --vpk <Project>.vpk --scene res://...`. Unchanged from the original pipeline.

## Web (WASM / WebGPU) — pipeline complete, rendering WIP

The web template bakes in **no** assets. At page load the runtime **fetches** the project's
`.vpk` over HTTP and mounts it in MEMFS, so a Windows machine can export to web with **no
Emscripten toolchain installed**.

Pieces:

- **Engine-only template build.** `xmake f -p wasm -m release; xmake build vultra-runtime`
  produces `vultra-runtime.{html,js,wasm}` with the runtime shell but no project
  `--preload-file`. An `after_build` hook copies them into `build/web-template/` as
  `index.html` + `vultra-runtime.js/.wasm` — the web analogue of the desktop
  `exportTemplatePath`. This is a **build artifact** (git-ignored under `build/`), not source;
  the source is the shell at `web/emscripten_vultra_runtime.html`.
- **Runtime-fetch shell.** [web/emscripten_vultra_runtime.html](../web/emscripten_vultra_runtime.html)
  reads `?vpk=` (default `game.vpk`) and `?scene=` from the URL, sets
  `Module.arguments = ['--vpk','resources.vpk','--scene', …]`, and in `Module.preRun` fetches
  the VPK into MEMFS at `/resources.vpk` as an emscripten *run-dependency* — so the engine waits
  for assets before `main()` runs. No C++ change is needed: `AssetSystem` already maps a relative
  `vpkFile` to `/resources.vpk` under `__EMSCRIPTEN__`.
- **Editor export.** `exportWeb` packs `outputDir/game.vpk`, copies the template into `outputDir`,
  and (for Export & Run) starts a local `python -m http.server` and opens the browser at
  `index.html?scene=…`. A server is required because `fetch()` is blocked on `file://`.

### WASM compatibility fixes

Making the engine actually link for wasm required cutting/replacing Vulkan- and desktop-only code:

- `requestXRSession` guarded behind `VULTRA_ENABLE_XR` (the only unguarded `m_XRBackend` use).
- `animation_system.cpp` uses ozz's portable `StorePtrU` instead of the raw `_mm_storeu_ps` SSE
  intrinsic.
- graphviz compiles with `_GNU_SOURCE` (emscripten's libc only declares `strdup`/`strndup` there).
- Ray-tracing RHI entry points (BLAS/TLAS/SBT/RT-pipeline) live only in the Vulkan backend; a
  guarded [raytracing_stub_no_vulkan.cpp](../source/vultra/src/core/rhi/raytracing_stub_no_vulkan.cpp)
  supplies inert definitions for non-Vulkan builds (they are never called: the RT feature flag is
  never enabled on a non-Vulkan device).
- The `joltphysics` package is built with the `rtti` config on wasm — `physics_system.cpp`
  subclasses `JPH::JobSystemWithBarrier`, and under clang/Itanium the derived-class typeinfo
  references the base typeinfo, which Jolt only emits when built with C++ RTTI. (Desktop/MSVC
  links without it.)

### WebGPU backend fixes (apply to desktop `--backend webgpu` and wasm alike)

The WebGPU RHI backend had never run a real frame before this work; getting it past validation
required:

- **HDR/float texture formats.** `toWgpuTextureFormat`
  ([conversions.hpp](../source/vultra/include/vultra/core/rhi/backends/webgpu/conversions.hpp))
  only mapped 8-bit + depth formats; added `RG16F/RGBA16F/R32F/RG32F/RGBA32F` (any HDR texture,
  e.g. the skybox, was previously `Undefined`).
- **`Float32Filterable` feature.** Requested at device creation when the adapter supports it, so
  the RGBA32F environment map can be sampled with a filtering sampler.
- **Depth format capability.** `WebGPURenderDevice::getFormatFeatureFlagsOptimal` advertised no
  depth formats, so depth textures were rejected and the depth attachment became `None` (→
  pipeline mismatch panic). Added the depth/stencil formats with the depth-stencil-attachment
  capability bit.
- **Load-op compatibility.** WebGPU has no `DontCare` load op; `AttachmentLoadOp::eDontCare` was
  mapped to `WGPULoadOp_Undefined`, which wgpu-native rejects for a used attachment. Now mapped
  to `Load` for color/depth/stencil.

**Still broken:** with the above, the WebGPU render loop runs without crashing but renders a
black frame (see Status banner). On WebGPU the universal renderer uses
`universal_compat.vrg.json` (compatibility forward path,
[universal_renderer.cpp](../source/vultra/src/function/rendering/srp/builtin/universal_renderer.cpp));
that path executes its passes but produces no visible output. Next step is per-pass debugging
(RenderDoc capture or instrumenting `final_composition_pass` / `compatibility_basecolor_pass`).

## Android — planned (not yet implemented)

Mirror [examples/android_app/](../examples/android_app/):

- **Template build.** Build `vultra-runtime` as `set_kind("shared")` →
  `libvultra_runtime.so` (arm64-v8a); entry `vultra_android_run`; link `libgame-activity.a`
  from the gradle prefab cache (the `on_load` hook in the `vultra-runtime` target already does
  this). Android uses the **Vulkan** RHI, not WebGPU.
- **Template package.** A zipped gradle skeleton derived from `examples/android_app/`
  (`*.gradle`, `gradle/wrapper/*`, `AndroidManifest.xml`, GameActivity glue) with placeholders
  for app name / package id and slots for `app/src/main/assets/game.vpk` and
  `app/src/main/jniLibs/arm64-v8a/libvultra_runtime.so`.
- **Asset delivery.** Unlike web, bundle `game.vpk` in the APK `assets/`; extract it to the app
  files dir on launch and point `vpkFile` there (until a `VpkFileSystem` AAsset backend exists).
- **`exportAndroid` (future).** `packProjectVpk` → unzip gradle template → drop the `.so` +
  `game.vpk` → invoke `gradlew assembleDebug/Release` with `ANDROID_SDK_ROOT` / `ANDROID_NDK`
  from new `BuildSettings.androidSdkPath` / `androidNdkPath` → emit the APK. Add
  `androidSdkPath`, `androidNdkPath`, `androidPackageId` and keystore fields to `BuildSettings`
  and the export settings UI.

Open risks: ship prebuilt `.so` per ABI vs NDK-compile at export; `VpkFileSystem` has no
AAssetManager backend yet; `libgame-activity.a` version must match the gradle-resolved
`games-activity` prefab; Gradle/JDK/SDK/NDK presence on the export machine; release signing.
