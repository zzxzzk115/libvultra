# Vultra Handoff

## Current state

The `dev-VRI` branch contains the early architecture migration and research milestones. Public usage remains one
static `vultra` library with direct VRI/RenderGraph access. `vultra-scripting` and `vultra-vgui` are optional.
All owned file/ABI versions remain `1`.

- RuntimeContext owns desktop resources and RenderingServer. Scene GPU RIDs, runtime ObjectIds and persisted IDs
  are distinct. SceneGpuSync updates transforms/materials without geometry reupload; topology/model changes rebuild
  at completed GPU boundaries. Camera/light/environment/material data and native/Lua/C# setters share scene state.
- EditorGui wraps ImGui; VGui wraps RmlUi with embedded PNG styles. Advanced RmlUi masks/transforms/layers/effects
  remain unsupported. Examples stay separate targets. The research workbench edits existing scene objects and graphs;
  it is not yet a complete topology-authoring editor.
- Native extensions, C++ node scripts, Lua and safe C# node scripts share the generated version-1 ABI. C# uses .NET
  10 and preserves serializable instance fields during reload; native/Lua local state resets. Public safe C# wrappers
  and discovery remain partly handwritten. Failed module replacement retains the old module.
- `vultra-pack` exports external or EXE-embedded project VPKs. Built-in shaders/OpenPBR are embedded separately.
  Archives currently extract to temporary files. Lua is static; native modules are files and C# needs .NET/host files.
- `ExperimentSession` is the shared windowless renderer. Optional `ExperimentHost` adds script updates, checked
  session IDs and the generated experiment table. `vultra-batch` and optional `vultra-research`/Python use this path.
  Version-1 `.vexperiment` fixes paths, camera, graph, seed, time step and finite warmup/measured frames. Both CLI and
  Python replay it. Script GUI callbacks/hot reload are disabled offline; scripts must make their own RNG deterministic.
- `PassCatalog` installs shared `vultra.tone_mapping`; project HDR compute → tone mapping has direct C++/JSON parity.
  Workbench/batch accept a primary HDR output or display-encoded RGBA8. RGBA8 is shown without a second tone map;
  capture its processed HDR by a marked port. Internal shadow/G-buffer/lighting catalog adoption remains incomplete.
- `ReferencePathTracer` runs in workbench, batch and Python using VRI ray query/bindless features. It provides
  progressive opaque OpenPBR transport, analytic/emissive/environment sampling, deterministic seeds and seven AOVs.
  State/shader changes reset explicit history. TLAS updates retain BLAS geometry. Texture filtering, wider material
  support, deformation motion and SDK-specific signals remain gaps; see `docs/reference_renderer.md`.
- RenderGraph supports explicit texture history and opt-in exact-description transient reuse. Imports, exports and
  history are excluded; an aliased compiled plan is immutable. Hierarchical CPU/GPU events and actual VRI allocation,
  resource/producer/image data appear in version-1 graph reports. Offline RenderDoc records and reports a real `.rdc`.
- Parallel all-target .NET builds compile the shared host/API through one `vultra-managed-host` dependency. Consumer
  script assemblies build with `--no-dependencies`, avoiding concurrent writes to the shared API output.
- The Linux delivery rule puts `--as-needed` around inherited libraries; trailing ldflags left unused desktop libraries
  in ELF NEEDED. Batch/player now directly need only Vulkan/libc/libm/the loader. The optional shared host also needs
  the system C++ runtime. Current development artifacts reference GLIBC_2.43; choose a release sysroot explicitly.

## Current Linux verification

SDL3/release/Vulkan; all rendering runs remove DISPLAY, WAYLAND_DISPLAY and VULTRA_WINDOW_SYSTEM. No native window
was opened. Prefer this path while the desktop is in use; Workspace 5 is reserved for necessary native-input tests.

- The all-target build passes. A selected suite passed 26 CPU/offscreen tests; its one failed selection was the native
  `test-camera/default`, which could not initialize SDL with no display. The added
  `test-camera/offline` passes input/controller math and preserves default native assertions. `test-gpu/offline` covers
  rendering/profiling without its final window section. These are not native-input acceptance.
- Cornell MSE against an independent 2048-sample image decreases 0.0433805 → 0.00271232 at 16 → 256 samples.
  Textures, emission, alpha mask, mirrored facing, analytic light PDFs, motion and all state/shader resets pass.
- CLI/Python experiment replay and parameter scans have exact same-device HDR readbacks; reference AOVs, owned
  NumPy arrays, invalid-input/replacement recovery and copied-library/VPK loading pass. Multilingual native/Lua/C#
  batch/VPK runs pass with no display or xmake in PATH.
- Workbench persistence, intermediate PNG/PFM, explicit display pass and raster/reference reopen parity pass;
  screenshots were inspected, including custom extents and disabled raster controls in reference mode.
- Offscreen RenderDoc produces a real `.rdc`; missing injection fails. Capture generation is verified; automated
  replay and Nsight inspection are not.
- Codegen consistency/signature tests, formatting of 130 modified project C/C++ files, focused clang-tidy and clangd
  LSP checks pass. Logs are `build/.tmp/milestones-*`. Negative shader/plugin/cache tests intentionally emit errors;
  standalone successful rendering logs have no unexpected GPU diagnostics.
- A read-only Fedora 44 container with no network/display/build-tool PATH passes batch/player startup, exact raster
  PNG/PFM parity, native/Lua VPK parity and reference AOV/sample checks. Only payload/output and explicit host GPU
  driver/validation mounts are present. Its kernel/GPU driver are shared with this machine; this is isolated-userland
  evidence, not another physical installation. Native modules require an executable extraction filesystem.

Rigid-object motion, truncated UTF-8 ABI errors and projection rejection with valid-frame recovery have passing
regressions. The reference tracer currently supports pinhole perspective cameras. Local evidence remains ignored in
`build/.tmp/milestones-*`; captures and experiment outputs are not source files.

## Remaining gates

Read `docs/research_milestones.md` and `docs/future_tasks.md` before selecting work. The latter audits actual sources
at dev-next commit `d8fe93850d7dbeeebc6992476566d7f70bc6ca86`; it does not authorize bulk ports. Resource streams,
Slang shader cooking/export templates and an optional offline browser stream precede larger engine features.

M7 is open: pinned VRI already implements D3D12, but Vultra selects Vulkan and static Slang disables DXIL. Settle
working DXIL cooking/compilation and validate on Windows hardware before exposing backend selection. Separate clean
Linux/Windows delivery, native GLFW/SDL3 input and Wayland/X11 acceptance also remain. Earlier Windows Vulkan
builds/tests and external/embedded VPK smoke runs predate these research changes. They are not current M0–M6
Windows acceptance.

Retain previous unresolved Hyprland requested-resize/detached-viewport assertions and interactive Wayland picking.
Physical XR is unverified. Prior simulator logs include pipeline-cache validation, missing timestamps/timeline
emulation and unsupported extensions; do not classify simulator smoke as clean hardware validation. A previous
.NET WASM AOT probe was only toolchain/ABI evidence, not a Web player. NVIDIA SDK passes, async compute and Web
remain separate work with explicit input/synchronization/delivery requirements.
