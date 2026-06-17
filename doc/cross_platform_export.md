# Cross-platform export templates

**English** | [简体中文](zh_CN/cross_platform_export_CN.md)

> **Status (2026-06):**
> - **Desktop (Vulkan):** working.
> - **Web (WASM / WebGPU):** the WebGPU backend now **renders** (the earlier black frame is
>   fixed). Reproduced and fixed on desktop `--backend webgpu`: missing single-channel texture
>   formats (`R8`, etc.) and a read-only-depth-attachment load-op were the backend bugs; the
>   *scene*-level black was a render-graph + material-decode issue (a project graph using the
>   bindless deferred path on a backend with no bindless, plus the compat pass reading material
>   params at the wrong byte offset). The renderer now uses **capability-aware render graphs**: a
>   single graph branches on `when` predicates (`feature_bindless`, `backend_webgpu`,
>   `platform_*`, …) so the deferred (bindless) path runs on desktop Vulkan and a forward
>   **base-color compat** path runs on WebGPU — verified rendering the full sponza scene on
>   `--backend webgpu` with zero validation errors. The compat path is intentionally
>   **base-color only** for now (no lighting/shadows/SSR, no skinning, no custom materials —
>   tracked as follow-ups). The architecture (editor-free `vultra-runtime` + runtime-fetch VPK +
>   engine-only template) is sound; remaining web work is end-to-end browser validation of the
>   exported bundle.
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
wasm and android and contains none of the editor / launcher / MCP / asset-import code. It is the
*source* binary; it also reaches users as a published release asset — a prebuilt runtime the editor
can download instead of building locally (see [Export templates / downloadable
runtime](#export-templates--downloadable-runtime)).

The editor's export dispatcher (`runBuildAndLaunch`) switches on `BuildSettings.targetPlatform`:
`Windows`/`macOS`/`Linux` → `exportDesktop`; `WebGPU` → `exportWeb`; `Android` → planned.

## Desktop

`exportDesktop` packs `<Project>.vpk`, copies the runtime executable next to it, and launches
`vultra --vpk <Project>.vpk --scene res://...`.

It resolves the runtime to copy in priority order
([editor_app_build.cpp](../source/vultra_app/src/editor_app/editor_app_build.cpp)):

1. an explicit `exportTemplatePath` from the export settings, if set;
2. otherwise an **official template downloaded** from the remote catalog for the target
   platform+arch+engine-version, resolved offline via
   `export_templates::cachedExportTemplate(...)` (see below);
3. otherwise the editor's own executable, but only when exporting for the host platform.

So the editor's exe is now just the last-resort source; the recommended path is a prebuilt
editor-free runtime (an explicit one, or a downloaded official template).

## Export templates / downloadable runtime

Export Settings can download a **prebuilt, editor-free `vultra-runtime`** for the target
platform+arch+engine-version instead of requiring a local build. The subsystem lives in
[export_templates_repository.hpp](../source/vultra_app/include/editor_app/export_templates_repository.hpp)
/ [.cpp](../source/vultra_app/src/editor_app/export_templates_repository.cpp).

- **Remote catalog.** A JSON catalog enumerates `ExportTemplateEntry` (platform + arch) → newest-first
  `ExportTemplateVersion` list (`version`, `minEngineVersion`, release-asset `url`, optional
  `sha256` / `size`). The catalog points at GitHub Release assets on
  `zzxzzk115/vultra-export-templates`; the binaries are **not** in the catalog repo and are
  downloaded on demand. `bestVersion` picks an exact engine-version match, else the highest version
  whose `minEngineVersion` is satisfied.
- **Cache layout** (rooted at the editor-global cache root):
  ```
  .vultra/export-templates/
    catalogs/<owner>-<repo>.json                    downloaded catalog (offline fallback)
    <platform>-<arch>/<version>/vultra-runtime[.exe]  materialized runtime binaries
  ```
  `fetchExportTemplatesCatalog` caches the catalog so it still renders offline; on a catalog miss the
  previously cached copy is used. `materializeExportTemplate` downloads a binary once (skipped if
  already present and the expected size matches).
- **Offline build resolver.** During export, `cachedExportTemplate(cacheRoot, platform, arch,
  engineVersion)` returns the already-cached runtime path with **no network access** (the
  "Download official template" button in Export Settings is what fetched it earlier). This is step 2
  of the `exportDesktop` resolution order above.
- **Release CI.** [.github/workflows/release_export_templates.yaml](../.github/workflows/release_export_templates.yaml)
  builds the editor-free `vultra-runtime` per platform/arch on a `v*` tag (or manual dispatch) and
  publishes it as a release asset on `zzxzzk115/vultra-export-templates`
  (`vultra-export-template-<platform>-<arch>-<version>[.exe]`, release tag `v<version>` from
  `xmake.lua`'s `set_version`). It requires an `EXPORT_TEMPLATES_TOKEN` PAT to push to the separate
  templates repo. The matrix ships **windows/x64** (`.exe`, windows runner), **android/arm64-v8a**
  (`libvultra_runtime.so`, ubuntu runner + NDK 30) and **wasm/wasm32** (a `.zip` of the engine-only
  web template `index.html` + `vultra-runtime.js/.wasm`, ubuntu runner + Emscripten). Android/wasm
  build on Linux where the dependency toolchain is reliable (a Windows host hits CMake-4.x/NDK and
  host-tool-arch issues for the cross deps).

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
