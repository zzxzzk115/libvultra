# Handoff

Dated snapshot: 2026-11-16, branch `dev-slim`. Verify claims against the code before relying on them.

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
- Render stages (`StageDesc`, `RenderStage`), the concrete `RenderPath` built from a path description and stage
  factories, `validateRenderPath()`, and `vultra.forward` as the stages `vultra.mesh` and
  `vultra.forward_shading`; a path takes a list of views (M8 part a, 2026-11-12).
- Graph usages `eVertexBuffer` and `eIndexBuffer` for buffers a draw reads.
- Path inputs `scene.lights` and `camera.data` (`CameraData`), owned by `RenderPath`; `Usage::eConstantBuffer`;
  `SceneResourceDesc::elementSize`; `PrimitiveTransform` public in `scene.hpp`; requested shader entries found in
  linked modules (M8 part b1, 2026-11-13).
- Native WebGPU allowed by `libvultra_graphics_api`; `ShaderPipeline` compiles WGSL for a WebGPU device. The Web
  build of the basic examples and native WebGPU were run on 2026-11-13; compute on WebGPU needs the local VRI branch
  `fix/webgpu-compute`. With it, `depth-pyramid` on native WebGPU matches Vulkan; its depth output is a struct member
  because Slang turns a return value marked `SV_Depth` into a WGSL color output.
- `xr` is built only with `libvultra_graphics_api=vulkan`, the one OpenXR graphics binding the module implements;
  the D3D12 and Metal bindings are a later milestone, and the Web would need WebXR. `Device::defaultGraphicsAPI()`
  and `shaderProfile()`; the builtin pack is cooked as WGSL in a WebGPU build, without the path tracer. The
  environment radiance is uploaded as `RGBA16_SFLOAT` so WebGPU can filter it (2026-11-13).
- M8 part b2 (2026-11-13): forward shading layouts within WebGPU limits (12 sampled textures per stage, no
  sampler arrays, the cascade shadow code removed); the Slang module `vultra.material` and `SceneMaterials`;
  surface layers on request (`StageBuild::requested`: `scene.normal`, `scene.albedo`, `scene.material`,
  `scene.emission`, `scene.motion` with object motion through `GpuScene::beginFrame()`); the `display` slot after
  the caller's passes (`RenderPath::addDisplay`, `vultra.tone_mapping`) and stage parameters; the environment's
  textures as path inputs; the builtin pack carries the sources of `vultra.material` (`extractBuiltinPack()`);
  RG16F readback. In Vultra-CLI, `[VultraDraw]` passes and render paths offered by package manifests; in
  Vultra-Packages, `vultra.render` with `vultra.deferred` as pass shaders.
- M8 part c (2026-11-16): material types. `IVultraMaterial` and `vultra.openpbr` in `vultra.material`; the builtin
  `vultra.materials` (dispatch over the types a program knows; only `vultra.openpbr` here); `scene.materials` and
  `scene.material_data` from `vultra.mesh`; push constants are now the draw's indices (`MaterialDraw`) and
  OpenPBR parameters are read from set 1; `GpuScene::setMaterialType`, `writeMaterials()` every frame from
  `RenderPath::prepare`; `forwardShadingStage(api, materialShaders)`; `kSceneSurfaceId` (R32_UINT material number,
  revised from RG32_UINT by the owner because VRI 0.2.2's WebGPU backend maps no RG32_UINT texture);
  `vultraSampleLight`. The builtin pack ships `passes/forward.slang`'s source so a host cooks it with its types.
  Templates of Vultra-CLI render byte-identically before and after on Vulkan, D3D12 and WebGPU.
- D55 segment (2026-11-17), the kernel side of native pass types, `--output-resource` and image atomics:
  `readbackTexels()` and `ImageReadback::consumeTexels()` return texels as stored (the unsigned formats R8 to
  RGBA32_UINT included), `readback()` decodes RG8, RGBA16, R16F and RG32F as well and refuses integer formats,
  `readbackBuffer()` copies a buffer back (`test-readback`); `ShaderPipeline::loadProgram(device, file, text, ...)`
  compiles source text for the device, so a native pass of a package names no shader target;
  `DeviceFeatures::imageAtomics32` (Vulkan and D3D12 when R32_UINT is a storage format; no VRI bit or request).
  The native pass types themselves (`Registry::addPass`) live in Vultra-CLI and need nothing new from the kernel.
- M8 part d1 (2026-11-17): render size and output size per view (`RenderView::renderSize`, `StageBuild::size` and
  `outputSize`, the `upscale` slot), subpixel jitter (`StageDesc::jitter`, Halton (2, 3), the path jitters the
  cameras it gives the stages; `CameraData::jitter` in pixels and `outputSize` in the former padding;
  `previousViewProjection` without jitter, so forward's and the sky's motion subtract it), `StageDesc::minimumScale`
  and `maximumScale`, `PathIssueKind::eDuplicateJitter`. The standard upscale stage, `vultra.temporal`, is a pass
  shader in Vultra-Packages.
- M8 part d2, kernel side (2026-11-17): auxiliary views (`StageDesc::auxiliaryViews`, `StageBuild::addView`,
  `AuxiliaryCamera`, `StageFrame::auxiliary`, `StageBuild::pathViews`, `PathIssueKind::eAuxiliaryView`) and the
  standard name `scene.shadow_mask`. The shadow map stage that uses them, `vultra.shadow_map`, is in
  Vultra-Packages.
- Build parameters (2026-11-17): `StageDesc::buildParameters`, `StageBuild::parameters` and
  `RenderPath::needsBuild()`, for stage parameters that size graph resources, such as the shadow map size of
  `vultra.shadow_map`.
- M13 first half, stereo rig, kernel side (2026-11-17): `CameraData` gains `tanHalfAngles`, `viewIndex` and
  `viewCount` (528 bytes); `GraphDefinition::build` takes a view name. The host's two-eye session, `.vheadset` and
  `--headless --stereo` are in Vultra-CLI. Derived views: `RenderView::derived` (the host's derive graph makes
  their `scene.hdr`). `views.data` (`ViewsData`, two views and a count; `[VultraViews]` in the host). History
  buffers (C1): `RenderGraph::createHistoryBuffer`, zeroed by a copy from one upload buffer on every API.
  `RenderView::slots`: a derived view runs the view stages of the slots it lists (C3, `stereo.derive_slots` in the
  host).
- M9 part a (done 2026-11-18): models and instances in `GpuScene` (`SceneModel`, `SceneInstance`, `SceneDraw`,
  `InstanceData`/`ModelData`, `scene.models`); named scene buffers (`addBuffer`, `buffer`); geometry processors
  cached with the model (`GeometryProcessor`, `ImportedAsset::buffers`, `StageDesc::geometryProcessor`); material
  sampling with explicit gradients (`vultraSampleMaterial`); stage factories receive `StageSources`;
  `StageDesc::optionalFeatures` with optional features in `Device::create` and `AppConfig`; `Usage::eIndirectBuffer`,
  `meshletBound`, `SceneMaterials::set`. `vultra.meshlets` and `example.visibility_buffer` are in Vultra-Packages.
- R42 (2026-11-18): shader keywords and cooked variants (`[VultraKeyword]`, `ShaderCompileOptions::keywords`,
  `compileVariants`); material types sample by slot (`vultraSampleMaterial(slot, input)`); material sets of
  descriptor arrays (`MaterialBinding::eArrays`, `SceneMaterials::arrayRanges`) for the visibility buffer's
  single-dispatch resolve on Vulkan and D3D12.
- M11, in progress: `loadPfm`, for `vultra compare`.
- Slang user-attribute and field-initializer reflection, stored in cooked programs.
- A render graph execution plan with batches per queue, async compute through `execute(Frame&)`, and
  `writeDot()`.
- Graph textures with mip chains, array layers, cubes and 3D extents; uses of one mip or layer, with
  dependencies, culling, history, handoffs and barriers per mip and layer; `Texture` state per mip and layer;
  readback of one mip and layer or depth slice; the `depth-pyramid` mode of `example-basics` (M7 part a, 2026-11-12).
- `Device::features()` and an audit of VRI capabilities in `docs/device_features.md` (M7 part b, 2026-11-12).
- `FileWatcher`, used by shader hot reload.
- Ray, plane, triangle and rectangle intersection helpers.
- `--libvultra_vri_dir=<checkout>` to build VRI from a local source tree.
- `ShaderCompileOptions::debugInfo` and `optimize`: shader debug information and SPIRV-Tools optimization per
  compile instead of per C++ build mode.
- `LightBuffer`: the frame's lights as one storage buffer (`scene.lights`), read by `vultra.forward_shading`.
- Import caches made through an `AssetSource` name files relative to its root and survive moving it.
- Errors as values: every fallible operation returns `std::expected<T, vultra::Error>`, fallible types are made by
  static `create()`, app hooks and RenderGraph pass callbacks return `std::expected<void, Error>`, and
  unreachable states call `vultra::panic()`. Remaining `catch` blocks only convert third-party throws
  (argparse, nlohmann_json, spdlog, `std::filesystem` via FileWatch) where Vultra calls them. On macOS the
  OpenXR loader's hidden `std::exception`/`std::logic_error` typeinfos made base-class catches fail, which is why
  catches name the exact thrown type.

## VRI

The default build uses the tagged VRI v0.2.2 release through the shared xmake `vri` recipe; `external/vri/`
holds no patches. The former patches, and the Metal patches previously carried by a downstream fork, were
merged upstream as pull requests 26 to 29 of [zzxzzk115/VRI](https://github.com/zzxzzk115/VRI/pulls). The
Vulkan changes were built and tested through this library; the Web, Metal and Android changes were not built
on the development machine and rely on VRI's own CI. The Linux desktop CI job of the tagged commit did not
reach its build because the runner could not resolve its package mirror; it passed on each pull request.

v0.2.1 (pull request 32) adds the two fixes macOS needs, pull requests 30 (the Vulkan backend no longer assumes
a 1.3 device; MoltenVK reports 1.2) and 31 (Metal reports point and range timestamps separately). With v0.2.0,
Vulkan found no adapter on macOS and Metal apps failed when the profiler was created. The `vri` recipe in
zzxzzk115/xmake-repo carries v0.2.1 since its pull request 13.

v0.2.2 (2026-11-16, pull request 37, tag commit `a6b5127`) merges pull requests 33 (WebGPU fixes, ETC2 and ASTC
4x4), 34 (64-bit atomics), 35 (identity surface transform) and 36 (Direct3D 12 backend fixes); `dev-slim` needs
all four, and the v0.2.1 package no longer compiles the kernel. The `vri` recipe carries v0.2.2 since
zzxzzk115/xmake-repo pull request 14.

## Verification

Windows 11, MSVC 2022, release, RTX 4090 Laptop (Vulkan, validation enabled), VRI built from the local checkout
with all four branches merged:

- `xmake build -y --all` and `xmake lua tests/build_modules.lua` (each module builds alone).
- Tests: render path (OpenPBR reference, material textures, live transforms, a second custom path), graph
  resources, graph definition, graph async (graphics and compute queues, parity with the single-queue plan),
  graph subresources (depth pyramids, array layers, cube faces, volume slices, history mips, rejected uses),
  shader program (attributes, cooking, watched reload), shader compiler, file watcher, ray, asset pipeline,
  asset formats, glTF materials, debug draw, meshlets, device, device selection, window, desktop, display,
  GUI offscreen/lifetime/settings, VPK memory, image, input map.
- `example-offline`, `example-ui` and the window tests ran for a finite number of frames.

macOS 27 (Darwin 27.2), Xcode 27 SDK, Apple M2, release, the VRI v0.2.1 package: `xmake build -y --all` and
every test target with `libvultra_graphics_api=vulkan` (MoltenVK from Vulkan SDK 1.4.309, validation enabled)
and `metal`. All pass except `test-meshlets` on Vulkan (MoltenVK has no mesh shading); it passes on Metal.
`example-basics vri` and `graph` and `example-ui` ran for a finite number of frames on both backends;
`tests/build_modules.lua` was not run, and clang-tidy covered only the sources the macOS build compiles.

The macOS work found that ISPC 1.28.2 drops a masked `n--` in bc7e's block encoder, so every solid-color BC7
block was wrong on every ISA; `external/bc7enc` carried a two-line patch. That encoder has since been replaced
by basisu (see WebGPU below).

Android, 2026-11-12, from a Windows host: `tests/platform/` (a separate project) and `example-basics` were built
for `x86_64` with NDK 30.0.16248370, packaged with the xmake scripts and run on the emulator (API 37); the
commands and what they found are in BUILD.md. The `arm64-v8a` builds ran on an AYN Thor (Adreno 740,
Android 13): the probe, `example-offline` and the three modes of `example-basics`. That found that a display
rotated against its panel is shown turned by 90 degrees; the fix is a VRI change (identity pre-transform),
verified on the device through `--libvultra_vri_dir`; it is VRI pull request 35, released in v0.2.2.

The `xr` module gained controller input (grip and aim poses, select and squeeze per hand) and `example-xr`
uses it. On the Meta XR Simulator the example submitted its frames with both controllers located; the buttons
were not pressed and no headset was used. The two shutdown messages of that runtime are described in BUILD.md.

Not verified: Linux, Web and any XR headset; of `tests/platform/` only the Android probe was built,
not the Web probes. The interactive feel of windows, hot reload and HiDPI was not judged by a person.

## Known gaps

See [roadmap](../roadmap.md). In short: standard scene resources beyond HDR and depth, profiling across
batches, XR input on a headset and mesh LOD. Optimizing the builtin shader pack was measured and not adopted;
the roadmap records the numbers.

## WebGPU

Snapshot 2026-11-15. Native WebGPU runs through wgpu-native, the browser through emdawnwebgpu. The work was
merged from the local `dev-slim-webgpu` branches into `dev-slim` here and into `main` of Vultra-CLI; it needs
VRI pull request 33 (see VRI above), and nothing of it is pushed.

- VRI pull request 33: descriptor ranges carry the storage or sampled format and
  `VriDescriptorRange_WriteOnly`; storage clears, layer readback, float32-filterable, 3D sampling and compute
  push constants on WebGPU; copies follow their region; the device's limits are reported (wgpu-native leaves
  `maxColorAttachments` 0, reported as the core default of 8). `VriFormat_ETC2_RGBA8_UNORM` and
  `VriFormat_ASTC_4x4_UNORM` are appended to the enumeration, because cooked shaders store format values; Vulkan,
  Metal and WebGPU map them, and WebGPU requests the BC, ETC2 and ASTC features the adapter has.
- Shaders: pass outputs are `[format("…")] WTexture2D` written with `Store`, internal and history images declare
  `[format]` (`vultra check` reports VS003 otherwise); pass parameters have no `bool` (VS004), and a `uint` in
  `[0, 1]` without option names is a checkbox.
- G-buffer profiles (2026-11-15, the owner's choice): `gbufferLayers(api)` in `render_path.hpp` lists them, and
  WGSL compiles with `VULTRA_GBUFFER_COMPACT`. The compact profile (WebGPU) fits the default 32 bytes per sample
  with `scene.hdr`: octahedral rg16f normal, rgba8 sRGB albedo (base color times base weight, occlusion in alpha),
  rg8 roughness and metalness, RGB9E5 emission in r32ui, rg16f motion; coat and specular color take OpenPBR's
  defaults in a deferred path there. The full profile (the other APIs) loses no precision: rgba16 material
  (roughness, metalness, specular weight, diffuse roughness), rgba16f emission, and `scene.coat` and
  `scene.specular` in rgba16f, seven layers and `scene.hdr` within eight color attachments. Pass shaders declare
  the layers with the format "standard" and the types of `vultra.gbuffer`. `scene.surface_id` and
  `scene.surface_uv` of contract 5.26 wait for their first user in M9. The owner plans to move to a visibility
  buffer with a thin G-buffer later, for bandwidth.
- Texture compression, following PlayCanvas's Basis Universal approach: the importer encodes linear textures
  whose size is a multiple of 4 as UASTC (basisu 1.16.4 replaces the bc7e encoder and ISPC); the upload
  transcodes them to the first of ASTC 4x4, BC7, ETC2 RGBA8 and RGBA8 the device samples. Authored DDS textures
  keep their BC format and fail to upload with an error where the device does not sample them. The local basisu
  recipe drops `add_vectorexts("all")`, which built the xmake-repo package with `/arch:AVX512`.
- Tests that depend on the device: `test-meshlets` checks that a device without mesh shading refuses the meshlet
  upload; `test-render-path`'s emission check multiplies the texel the device samples, because the Intel Raptor
  Lake iGPU decodes sRGB about 0.2% above the exact curve. Both tests are also on `dev-slim`.
- The Web `example-offline` embeds the example pack and loads its shaders from it.

Verified on Windows 11 (RTX 4090 Laptop, and its Intel Raptor Lake iGPU through
`VK_LOADER_DRIVERS_DISABLE=*nv*`):

- libvultra tests: 35 of 35 on native WebGPU, on Vulkan with the RTX 4090 and on Vulkan with the Intel iGPU.
  On the Intel iGPU the UASTC sampling check passed with each target forced in turn (ASTC, BC7, ETC2, RGBA8).
- Vultra-CLI 11 of 11, `vultra.render`, `lab.passes` and `vultra.lua` on native WebGPU.
- Browser (Chromium-based, Windows): `example-basics` `window`, `vri`, `graph` and `depth-pyramid` render without
  WebGPU errors; `example-offline`'s `linear.pfm` is byte-identical to the native WebGPU run.

Not verified: Metal (the ETC2 and ASTC mapping and the Apple-family check were not compiled), Android, ETC2 or
ASTC through WebGPU (this machine's wgpu-native adapter offers BC only), and UASTC transcoding in the browser,
whose examples load no imported textures. A page loaded while the browser was hidden reported "WebApp requires a
nonempty canvas at construction"; a reload worked.

## Direct3D 12

2026-11-15. `libvultra_graphics_api=d3d12` builds on Windows (`BUILD.md` has the user-facing notes):

- Shaders are DXIL, profile `sm_6_6`. Only this configuration builds `slang-static` with its `dxil` config; Slang
  loads `dxcompiler.dll` at run time, and a missing DLL is an error that names it. Adding the config changed the
  package hash, so every configuration rebuilds `slang-static` once with the same contents.
- VRI v0.2.1 cannot run the kernel on D3D12. The fixes are VRI pull request 36 (`fix/d3d12-backend`), released in
  v0.2.2.
- VRI's D3D12 register mapping was kept by the owner: root constant and storage buffers at the range's
  `baseRegister`, table resources counted from 0 per class in range order, push constants at `b<baseRegister>`
  in space 0. The forward shaders pin their registers to it; the material push constants are left to Slang
  (`b2`), because an explicit `register()` on a push-constant block adds a descriptor slot to the SPIR-V and WGSL
  reflection.
- Verified on this machine (RTX 4090 Laptop, debug layer on): the 34 test programs pass on D3D12, Vulkan and
  native WebGPU. The cooked SPIR-V and WGSL of `forward.slang` and `skybox.slang`, reflection included, equal
  those of the parent commit. The `example-basics`, `example-ui` and `example-offline` captures are
  byte-identical between the parent commit and this one on Vulkan and on WebGPU, and between D3D12 and Vulkan.
  The probe before this integration found the forward HDR readbacks of `test-render-path`, `test-gltf-materials`
  and `test-asset-formats` bit-identical to Vulkan in 294 of 308 frames (largest difference 2.4e-4).
- Not covered: `xr` stays Vulkan-only; Vultra-Packages shaders do not pin D3D12 registers; no other GPU, WARP or
  DXC version was tried.

2026-11-16, branch `feat/multi-backend` (with Vultra-CLI and Vultra-Packages branches of the same name; started as
the D3D12 port of those two, then reframed by the owner into backends chosen at run time):

- Reflection of DXIL programs reported binding 0 for every resource and no push constants. It now records the
  D3D register classes (`ShaderOffsetKind::eConstantBufferRegister` to `eSamplerRegister`) and marks push
  constants from a SPIR-V layout computed in the same Slang session (no SPIR-V is emitted). `test-shader-program`
  checks registers, slots and push constants on the device's API.
- `openPbrLuts` is no longer pinned to `t4`, which only forward shading's frame set matched; Slang's first-fit
  still gives it `t4` there.
- The IBL targets are created with the opaque-black clear value their passes clear to; this removed the D3D12
  debug-layer warning from every host run. The kernel tests create each target they clear to one value with that
  value (`tests/support/cleared_texture.hpp`). Only `test-graph-resources` and `test-graph-subresources` still log
  the warning (52 lines): they clear one texture, or aliased textures, to several values on purpose to observe
  pass order and aliasing, which no single clear value matches.
- Several backends per build (`libvultra_graphics_apis`), chosen at run time: `Device::graphicsAPIs()`,
  `Device::create()` over a list or automatically, `VULTRA_GRAPHICS_API`, `AppConfig::graphicsAPIs`. The owner's
  decisions are D50–D53 in vultra-plans (v45). OpenGL is accepted on the Web only; the GL milestone wires it.
- One `.vshaderc` holds a program per target (`load(file, profile)`, `loadTargets`, `save(file, targets)`, `cook`
  with profiles); the builtin pack carries every target of the build. `shaderSetLayout()` lays out a set from
  reflection for an API; `ShaderPipeline::loadProgram()` serves callers that lay out before building.
  `ShaderProgram::hasDebugInfo()` answers per target.
- Building with `d3d12` (the Windows default) needs `dxcompiler.dll` on `PATH` at build time too: the builtin pack
  is cooked for DXIL.
- Verified with one build of `vulkan,d3d12,webgpu`: 34/34 on each API (D3D12 with the clear-value warnings above);
  the SPIR-V and WGSL of the builtin pack equal those of single-API `dev-slim` builds byte for byte.
- `vultra.deferred` (Vultra-Packages) logged one WebGPU validation error ("Error matching ShaderStages(FRAGMENT)
  shader requirements against the pipeline"): Vultra-CLI built a pass's pipelines before it knew the formats of
  the textures it samples. Vultra-CLI now builds them when the pass joins a graph.

## Local state

The uncommitted shadow receiver-culling work that existed on `dev-VRI` is kept as a git stash on the original
machine ("shadow receiver culling WIP"). It targets the removed renderer and is not part of this branch.
