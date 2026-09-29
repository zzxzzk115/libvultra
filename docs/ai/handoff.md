# dev-VRI Migration Handoff

Last reviewed: 2026-09-29. Audience: AI assistants continuing authorized work on libvultra's `dev-VRI` branch.

Read [AGENTS.md](../../AGENTS.md) for development constraints. This file records a dated migration assessment and pending work, not an instruction to implement the entire backlog. Recheck the relevant code before acting and preserve unrelated working-tree changes. The maintainer authorized cleanup, grouped local commits and pushing the completed `dev-VRI` branch after verification.

## Goal and Scope

The goal is a small, readable VRI-based rendering research framework that the maintainer can modify without depending on AI. Preserve the old libvultra `dev` branch's `core / function / platform` organization and the xmake-template build approach. `dev-next` and VRF are references for specific problems, not architectures to copy wholesale.

- Current target: Windows x64, Vulkan, desktop and OpenXR sharing VRI rendering infrastructure.
- Priorities: opaque rendering correctness, reusable built-in functionality, representative examples and efficient asset import/loading.
- Keep the current plain Scene data and composed renderer/camera objects. ECS and scene architecture are deferred; the maintainer will decide later where ECS or OOP is appropriate.
- Transparency and Gaussian Splatting are excluded. A graph editor, Python/Lua frontends, full OpenPBR conformance, animation and a production path tracer are not completion requirements for this iteration.
- Keep the code-driven RenderGraph. Do not restore the external fg dependency or introduce a second rendering abstraction above VRI.
- Original example assets should remain unchanged. Cache generated content rather than duplicate source geometry, source image levels or authored DDS mip chains. Private complex scenes may be used for local tests but must not be copied into the repository.

## Assessment

The migration is a usable research baseline, but is not complete enough for final acceptance. Most requested execution paths exist and have meaningful tests. The remaining work concerns material fidelity, platform boundaries, loading costs and reproducible acceptance. It does not require a wholesale rewrite or an ECS decision.

| Area | Current state |
| --- | --- |
| Application infrastructure | BaseApp, DesktopApp and ImGuiApp lifecycle; argparse CLI; spdlog logging |
| Input and cameras | Per-window Input state; built-in Orbit/FPS controllers under `function/camera`; examples reuse them |
| GUI | ImGui docking branch, multiple native viewports, themes, Unreal default, AppName-specific layout persistence |
| Shaders | Slang modules grouped into `lib`, `resources` and `passes`; FileWatch reload preserves the last valid pipeline on failure |
| RenderGraph | Small code-driven graph with resource validation, pass culling and barriers; single queue and limited resource/subresource scope |
| Built-in rendering | Skybox, HDR IBL, opaque OpenPBR subset, CSM and Hard/PCF/PCSS; normal and emission textures; indexed and meshlet paths |
| Assets | Static glTF/GLB, OBJ and FBX; DDS; derived texture cache; vtask jobs; SIMD BC7; build-time preparation and import progress logs |
| Research utilities | PNG capture, frame dumps, SSIM/PSNR, CPU/GPU profiling and title statistics |
| Examples | Main old-dev categories present, including original Sponza assets, ray query, ray tracing, mesh shading and XR; renderer parity remains partial |

Human-facing contracts remain in the [development guide](../guide.md), [asset pipeline guide](../asset_pipeline.md) and [example coverage](../example_parity.md). Keep actual supported limitations there; keep continuation status and task planning here.

## Outstanding Work

### High: Preserve basic opaque material semantics

The glTF loader does not preserve sampler state or the material's `doubleSided` flag. The built-in renderer uses one linear/repeat sampler for all material textures and `VriCullMode_None` for geometry. Clamp, nearest-filter and single-sided assets can therefore render incorrectly without an import error. These are basic opaque-material issues, unrelated to transparency or ECS.

Starting points: [SurfaceMaterial](../../source/include/vultra/function/renderer/scene.hpp), [glTF material import](../../source/src/function/renderer/gltf_loader.cpp), [material samplers and geometry pipelines](../../source/src/function/renderer/builtin/builtin_renderer.cpp), [surface shading](../../builtin/shaders/resources/gpu_material.slangh).

Completion criteria:

- Preserve per-texture sampler semantics through import and rendering; shared images must not erase distinct sampling requirements.
- Preserve sidedness in indexed, meshlet and shadow paths, with matching back-face shading behavior.
- Add focused image regressions for clamp/repeat, nearest/linear and single/double-sided geometry, including mirrored transforms where relevant.
- Retain authored tangents. Missing tangents currently use custom accumulation and orthogonalization; add reference coverage for generated tangent-space normal mapping before claiming general fidelity.

Only untransformed TEXCOORD_0 is currently accepted, and clearcoat textures are rejected. These explicit restrictions must remain visible. Additional UV sets and extensions can be scoped separately; do not silently claim their support.

### High: Close the OpenXR validation gap

Meta XR Simulator renders the examples, but the latest run still reports:

```text
vkCreatePipelineCache(): pCreateInfo->flags includes
VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
but pipelineCreationCacheControl feature was not enabled.
```

The simulator also reports unavailable timestamp queries and timeline-semaphore emulation. Successful rendering does not clear the pipeline-cache validation error. Trace the runtime/VRI device feature contract before changing feature flags; do not suppress validation or label the simulator run as headset validation.

Starting points: [OpenXR implementation](../../source/src/function/openxr/openxr.cpp), [VRI device creation](../../source/src/core/rhi/device.cpp), [XR sample lifecycle](../../examples/common/xr_sample.cpp).

Completion criteria: resolve the feature/flag mismatch, rerun finite-frame eye/mirror rendering with diagnostics enabled, and report physical-headset testing separately. Keep desktop and XR color-space contracts intact.

### Medium: Finish the Window and GUI platform boundary

Input and camera types are independent of GLFW, SDL and ImGui. Window still exposes `GLFWwindow*`; DesktopApp directly calls GLFW for minimization and event waits; Gui directly initializes the GLFW backend and wraps GLFW viewport handles. SDL3 is not implemented and is not yet a drop-in backend.

Every independently owned Window currently calls `glfwTerminate()` in its destructor. Destroying one of several owned windows would terminate GLFW for the others. ImGui detached windows use borrowed wrappers and avoid this particular destructor path.

Starting points: [Window interface](../../source/include/vultra/core/os/window.hpp), [Window implementation](../../source/src/core/os/window.cpp), [DesktopApp](../../source/src/function/app/desktop_app.cpp), [Gui](../../source/src/function/renderer/gui.cpp), [GUI viewports](../../source/src/function/renderer/gui_viewports.cpp).

Completion criteria: isolate platform operations behind the window/backend boundary, define runtime ownership across windows, and preserve input callback chaining, focus handling and borrowed viewport lifetimes. Test independent-window destruction if multiple owners remain supported. Do not add a backend switch that has no working implementation or introduce a service framework to solve this boundary.

### Medium: Measure and reduce warm-loading costs

The derived-only cache policy is intentional and correct for the requested disk usage. Build dependencies move mip generation and BC7 encoding out of ordinary startup. At runtime, source geometry/material parsing and image decoding still happen before cache restoration. Generated geometry attributes, meshlets and HDR/IBL results are not persisted. Texture and buffer uploads each submit and wait. Import is synchronous at the application boundary despite internal vtask parallelism.

Starting points: [cache restoration order](../../source/src/function/asset/asset_pipeline.cpp), [GPU scene construction](../../source/src/function/renderer/scene.cpp), [meshlets](../../source/src/function/renderer/meshlets.cpp), [upload helpers](../../source/src/function/renderer/upload.hpp), [environment processing](../../source/src/function/renderer/builtin/environment.cpp).

Next steps when performance work is authorized:

- Measure parsing, decoding, generated geometry, texture preparation, uploads and IBL separately on cold and warm runs. Do not reuse old Bistro timings as current measurements.
- Batch uploads and completion points while keeping staging memory alive until GPU completion.
- Consider caching generated tangents, meshlets and IBL only where measurements justify it; preserve source bytes and derived-cache invalidation guarantees.
- Retain progress logs and error propagation. vtask jobs alone do not make model loading nonblocking for the application's UI.

### Medium: Complete example and migration acceptance

Example-category coverage is not full old-dev renderer parity. Sponza uses the OpenPBR forward renderer; the original deferred lighting setup, point lights and LTC area lights are not ported. Ray examples currently render static opaque untextured geometry. The accepted subset needs explicit visual checks and documented differences, rather than automatically adding every missing feature.

XR lifecycle and the ray-tracing app remain in `examples/common`. Promote infrastructure into the library only when a second research application would otherwise need to copy it; do not move every sample helper into the framework.

Starting points: [coverage table](../example_parity.md), [XR lifecycle](../../examples/common/xr_sample.hpp), [ray-tracing app](../../examples/common/ray_tracing_app.hpp).

Completion criteria:

- Provide repeatable finite-frame example checks with fixed assets, camera, lighting, exposure and color-space settings. Separate intended renderer differences from regressions.
- Verify a fresh checkout without relying on this machine's existing build/package caches. This has not been established by the current validation.
- Keep the representative assets and attribution intact. Keep captures, cache data and personal ImGui layouts out of source control.
- Provide a reproducible local verification entry point. No `.github/workflows` directory was present at this review; GPU CI is optional, not a reason to add another maintenance framework.

## Commit Preparation Verification

The migration snapshot was checked again on 2026-09-29 before preparing grouped local commits:

- `xmake build -y --all` passed, including all four example asset-preparation dependencies with current caches.
- Default parallel `xmake test -y` passed all 12 tests.
- clang-format passed for all 131 project-owned C++ source/header files.
- clang-tidy passed for all 77 project-owned C++ translation units with no project diagnostics.
- Human documentation and handoff file links resolve. Personal editor settings, asset caches, layouts, logs and captures remain ignored.

Upstream FileWatch and BC7 source formatting and the Damaged Helmet license texts are retained, including existing trailing whitespace; they are not part of the project's formatting claim. Sponza and Cornell Box text-file differences from the original Git tree were line endings only. This verification used the existing local package/build environment and does not establish fresh-checkout or physical-headset acceptance.

The commit groups separate editor/tooling configuration, vendored dependencies, the coupled framework/example/build migration, and documentation. Keep the dependent framework and example changes together so the build configuration does not refer to missing implementation files.

## Earlier Verification Baseline

These results precede the assessment and this documentation update; they are not newly rerun checks or a guarantee about future edits:

- Full `xmake build -y --all` passed.
- Default parallel `xmake test -y`: 12/12 passed, including camera input, application lifecycle, GUI, asset/cache, rendering, meshlets and debug-draw regressions.
- clang-tidy: 18 affected translation units had no project diagnostics. This is not a repository-wide static-analysis claim. Changed C++ files passed clang-format checks.
- Finite-frame runs and captures completed for glTF Viewer, Debug Draw, Sponza, meshlet Sponza, Ray Query, RT Triangle, RT Cornell Box and OpenXR Sponza.
- The seven desktop runs had no captured shader/VRI errors. OpenXR used Meta XR Simulator: 60 application frames, 59 eye frames, with the diagnostics listed above. Physical-headset validation remains outstanding.
- Previous asset checks covered cold/warm build preparation, shared targets, unchanged cache archives on a warm build, runtime hit/miss image parity and cache-publication failure.

The camera regression drives installed GLFW/ImGui callback chains, including scroll, key translation and focus loss. Its focus events are scripted so concurrent test windows do not steal the test's input; application focus handling remains real. GPU readback checks wheel zoom, no repeated scroll, and UI input capture. Do not mistake this test isolation for a production focus workaround.

Local historical captures/logs may still exist under `build/.tmp/camera-input-checks/1790710979078297800/`, with tidy logs under `build/.tmp/camera-input-checks/tidy/`. These ignored artifacts are optional evidence, not checkout prerequisites. Diagnostic checks must recognize both `[VRI]` and `[VRI ...]` formats; exit status alone is insufficient.

## Continuation Notes

- Work in the actual `libvultra-vri` checkout under `GitHubProjects`, where build/run permissions were established. Do not execute a staged output copy as though it were the active checkout.
- Run xmake with that repository as the working directory. Model examples depend on their specific asset targets; `example-assets` aggregates all default model assets. `run.autobuild` is explicitly enabled.
- Keep UI construction in `onImGui()`. Cameras read `Window::input()`; ImGui supplies only capture flags. Do not restore example-local camera implementations or source camera motion from ImGui transient input.
- Use isolated run directories/layouts and finite frame limits for validation. Do not overwrite the maintainer's scene files, captures, cache experiments or UI layouts.
- Keep human-facing documentation in `docs/` outside this directory, and AI handoff/work-state notes in `docs/ai/`. All repository documentation remains English.
- After an authorized item is completed, update its status and actual verification evidence here. Keep unresolved limitations explicit; do not mark the migration complete merely because examples launch.
