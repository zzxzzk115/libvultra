# Follow-up tasks

The integrated E0–E7 engine work and M0–M7 research gates are tracked in
[research_milestones.md](research_milestones.md). Implement E0 properties, E1 attachments and E3 editing as the
first combined slice; the platform gates below remain acceptance work, not a reason to defer that core.
This list separates unfinished acceptance from additional engine/research features. Items here do not create
modules or dependencies.
VRI, the existing RenderGraph, one public static `vultra` library and direct C++ use remain the boundaries.

## Close the current gates first

| Priority | Task | Concrete acceptance |
| --- | --- | --- |
| 1 | D3D12 research acceptance | Windows Vulkan workbench/offline/Python/VPK regression now passes. First settle DXIL delivery: the pinned static Slang package currently disables DXIL. Enable an implemented VRI D3D12 path only with working shader cooking/compilation and device capability checks; compare the same experiment/AOVs against Vulkan. |
| 1 | Clean-machine delivery | External and embedded VPK player, batch and optional research host on separate Linux/Windows installations. Choose the template's minimum glibc/target sysroot explicitly; record loader/driver/runtime requirements and optional .NET/native module files. Isolated Linux userland checks and copied binaries are useful evidence, not separate-machine acceptance. |
| 1 | Remaining desktop behavior | GLFW/SDL3 with X11/Wayland, resize, minimize, input, detached viewports and HDR picking. Linux Hyprland resize/viewports have prior unresolved failures; retain their assertions. Use offline testing except when a native-window test is necessary. |
| 2 | Further pass contracts | Shadow/skybox/G-buffer/deferred lighting now compose through real catalog ports with default-render parity and intermediate-only culling. Add other stages or non-scalar parameters only for a concrete experiment. |
| 2 | Reference signal/correctness breadth | The opaque subset has independent upstream C++/GPU furnace and filtered-environment integration, analytic/convergence fixtures and importance sampling. Mip 0 is the explicit reference texture model; optional footprint filtering and further material domains require separate contracts. Add orthographic ray origins when the camera API exposes that model. Verify deformation motion and SDK-specific depth/jitter conventions; add separated diffuse/specular signals and hit distances before claiming NRD/DLSS-ready inputs. |
| 2 | Further research APIs | Typed Python scene edits, shared report provenance and AOV mapping/probes are implemented through the existing ABI. Add topology/batch simulation controls only with an actual experiment and the same ownership/thread/phase rules. |
| 2 | Diagnostic validation | Automated RenderDoc replay and Nsight Graphics capture inspection; timestamp/memory behavior on a second backend. Keep capture runs distinct from timing baselines. Async compute requires measured benefit and real VRI queue/fence/barrier validation first. |
| Optional | NVIDIA passes | NRD, DLSS/DLAA and later RTXDI/Ray Reconstruction/Frame Generation are separate optional targets. Define actual signals, VRI interop and vendor-file export checks first. None are linked into the base player by default. |

## Features verified in dev-next source

The comparison uses the locally available `dev-next` snapshot
[`d8fe93850d7dbeeebc6992476566d7f70bc6ca86`](https://github.com/zzxzzk115/libvultra/tree/d8fe93850d7dbeeebc6992476566d7f70bc6ca86).
The paths below contain implementations, not only README promises. This audit did not build or rerun that branch.
Its service locator, old RHI/FrameGraph, EnTT world, global state and shader packaging are not migration templates.

| Order | Verified implementation | Reuse the behavior in the current design |
| --- | --- | --- |
| A | [Cooked asset registry and VPK-backed VFS](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra/src/function/asset/asset_registry.cpp) resolve UUIDs, populate a registry from the package and mount resources. | Offline and project-runtime CPU asset reads are implemented through `AssetSource`, preserving asset IDs, import/cache formats and GPU server ownership. File/VPK pixels match without whole-project extraction; native/managed dependencies are materialized selectively. VGui also reads RML/RCSS, fonts and PNGs directly; remaining work is the embedded-engine bootstrap and Lua module-stream loading. Evaluate `vasset` only against concrete cooking requirements. |
| A | [Export packing](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra_app/src/editor_app/editor_app_build.cpp) collects enabled scenes and plugins, omits editor-only plugins and copies explicit desktop/Web templates. [Template repository](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra_app/src/editor_app/export_templates_repository.cpp) resolves platform/architecture distributions. | Built-in and explicit project SPIR-V shader cooking, typed game/native shader assets and source-free shader VPKs are implemented. Extend the existing no-xmake export API with DXIL, explicit template metadata and dependency closure. Test one-EXE embedding, external VPK and optional vendor sidecars independently. This inspected desktop exporter writes `resources.vpk` beside the EXE; it is not evidence of project-VPK embedding. |
| B | [Frame/video recording](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra_app/src/editor_app/runtime_mcp_recording.cpp) has a bounded raw-frame queue, JPEG worker, MJPEG clients and video-encoder piping. [Python client](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/tools/python/vultra_client/README.md) consumes streamed frames. | Build an optional browser preview over the existing offline session with bounded readback/encoding and explicit cancellation. Begin with a local HTTP MJPEG frontend; authentication/network exposure is a separate deployment choice. Neither MCP nor an AI service becomes a framework prerequisite. |
| B | [Simulation tools](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra_app/src/editor_app/runtime_mcp_sim_tools.cpp) implement reset/seed and deferred fixed-step simulation. | Extend the generated experiment session API with reset, actions and named observations when actual simulation state exists. Test reset equivalence and action-to-render ordering. Python/NumPy remains an optional external research client. |
| C | [Material graph compiler](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra/src/function/material_graph/material_graph_compiler.cpp) compiles typed graph nodes to shader expressions. | Use a small validated IR generating Slang/OpenPBR inputs, with texture slots and parameter layouts shared by CPU/shader code. Add cycle/type diagnostics, last-good shader replacement and material-image tests before building a large editor. Do not import the GLSL/vshadersystem pipeline. |
| C | [Ozz animation](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra/src/function/animation/animation_system.cpp) evaluates sampling, blending and local-to-model jobs. | Add an optional AnimationServer and explicit scene skeleton/animation resources. Define skinning layouts, dirty pose synchronization and packaged clip reads. Verify fixed-time pose and GPU image parity before state-machine tooling. |
| C | [Jolt physics](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra/src/function/physics/physics_system.cpp) manages bodies, constraints and virtual characters. | Optional PhysicsServer with context-owned IDs, a fixed-step update and scene-node adapters. Test body destruction, transforms, contact events and reset. RenderingServer and SceneTree retain their own authority; avoid a second ECS world. |
| C | [miniaudio](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra/src/function/audio/audio_system.cpp) supports clips, spatial playback, one-shots and music. | Optional AudioServer with explicit clip/voice ownership and resource-stream decoding. Verify stopped callbacks on teardown and browser audio activation separately. Keep it outside core drawing examples. |
| D | [Gaussian splatting feature](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra/src/function/rendering/srp/builtin/features/general_gaussian_splat_feature.cpp) composes preprocessing, rendering and optional layered foveated outputs. | Start with an independently linked project pass and CPU splat asset; implement Slang/VRI buffers, ordering/compositing and image fixtures. Add XR/foveated layers only after the basic pass works. |
| Separate project | [Web exporter](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/source/vultra_app/src/editor_app/editor_app_build.cpp) copies an explicit Web template and VPK; [Web shell](https://github.com/zzxzzk115/libvultra/blob/d8fe93850d7dbeeebc6992476566d7f70bc6ca86/web/emscripten_vultra_runtime.html) provides browser packaging. | Verify VRI WebGPU, WGSL shader cooking, asynchronous browser frames, package mounting and script delivery. Earlier .NET WASM AOT probes establish toolchain feasibility only. The current desktop Vulkan/SPIR-V player is not a Web player. |

## Additional project work

Scene topology authoring, script attachment/discovery, native/Lua reload-state transfer, undo/redo and asset
dependency inspection belong to E1–E4 and consume the same Node/Resource and generated property model.
Safe C# camera/light/environment/material value types and their ABI conversion now come from the existing IR;
node wrapper discovery/identity and script-class metadata remain E0/E1 work.
Advanced RmlUi masks/transforms/layers/effects need backend implementations and image/input fixtures; current VGui
support must not silently claim those capabilities. TAA, SSAO, SSR, OIT and research algorithms should be catalog
passes using explicit history/AOV contracts, added one at a time with baselines.

Preserve the separation between `vultra-runtime` and editor code. Lua remains static in the base player; C#'s .NET
host and native plugin files have explicit delivery requirements. C# AOT, Web and physical XR each need their own
verified delivery path. Every new persisted format starts and remains at `1` before release; no migration layer.
