# Vultra Handoff

## Current state

`dev-VRI` retains one public static `vultra` library and direct VRI/RenderGraph access. Scripting and VGui are
optional static targets. All owned file and ABI versions remain `1`; changes deliberately break older files.
Human-facing contracts and commands are in `docs/guide.md`, `docs/architecture.md` and `docs/shader_system.md`.

- RuntimeContext owns desktop resources, RenderingServer, PassCatalog and ObjectTypeCatalog. GPU RIDs, runtime
  ObjectIds and persistent asset/node IDs are distinct. SceneGpuSync applies transform/material changes without
  geometry reupload; mesh membership/model changes rebuild after GPU completion. Native/Lua/C# setters share
  the scene's camera, light, environment and material state.
- EditorGui wraps ImGui; VGui wraps RmlUi with embedded texture styles. VGui supports window-backed and offscreen
  construction, but advanced masks/transforms/layers/effects remain unsupported. Its event bindings detach before
  their callbacks expire. The workbench edits existing scene objects and graphs, not arbitrary scene topology.
- AssetSource owns an explicit directory or VPK. Player, batch/Python, CPU importers, cooked game shaders and
  VGui read project resources directly. Package paths identify entries, not extracted files. Native/.NET sidecars
  and currently Lua scripts are materialized selectively and removed after script hosts stop. Only the embedded
  engine-shader bootstrap still extracts its resource pack.
- Native extensions, C++ node scripts, Lua and safe C# node scripts share the generated ABI. C# uses .NET 10;
  its current reload host preserves serializable instance fields, while native/Lua local state resets. Failed
  replacement keeps the old module. General node wrappers/discovery remain partly handwritten. C# player exports
  require .NET/host files; the base native/Lua player has no engine DLL sidecars.
- The first E0 slice supplies PropertyValue, typed accessors, defaults, JSON codecs and an explicit type catalog.
  Generated descriptors belong to their camera/light/environment, material and renderer modules. Inspector and
  persistence consume them; scene setters still validate domain relationships and mark revisions. Safe C# settings
  records/conversions now come from the same libclang IR. There is no global registry or second binding pipeline.
  Workspaces save all annotated renderer settings: `renderer.path` is numeric and `seed` is top-level. Scene JSON
  retains its existing keys/color arrays. This workspace change is deliberately version-1 breaking.
- ExperimentSession is the shared windowless renderer; optional ExperimentHost adds scripts and checked session
  handles. CLI/Python replay version-1 experiment descriptions. Offline script GUI callbacks and hot reload are
  disabled; scripts must make their own RNG deterministic. Batch/player can run without xmake on the target.
- PassCatalog installs shared tone mapping; project HDR compute has direct C++/JSON readback parity. HDR and
  display-encoded RGBA8 outputs are supported. Internal shadow/G-buffer/lighting catalog adoption is incomplete.
  ReferencePathTracer supplies progressive opaque OpenPBR transport, deterministic seeds and seven AOVs; remaining
  material/filtering/motion limits are in `docs/reference_renderer.md`.
- RenderGraph has explicit texture history, opt-in exact-description transient reuse and hierarchical CPU/GPU
  events. Reports associate resource allocation, producers and images. Offline RenderDoc creates a real `.rdc`;
  automated replay and Nsight inspection remain unverified. Async compute is not implemented.
- Game `.vshader` and native `.slang` sources cook to typed, checksummed `.vshaderc` programs. SPIR-V reflection
  owns shader layouts. Packaged built-ins use cooked programs without watchers; source editing retains Slang and
  FileWatch. Failed compilation/publication retains the prior valid artifact/pipeline. Linux dependency watches
  cover include precedence without recursively watching build/metadata trees; synchronous reload publication
  handles delayed notifications. The pinned VRI cube-array patch is documented in `external/vri/README.md`.

## Verified evidence

Current Linux work uses SDL3/release/Vulkan. GPU checks are offscreen with display variables unset; no desktop
window or workspace switch was used. Workspace 5 is reserved for necessary native-input checks.

- `build/.tmp/pull-offline-m3LE3l/`: all-target build, 36 CPU/offscreen regressions, shader include/reload failure
  recovery, Surface/cooked mesh/cube-array readbacks, standalone batch/workbench/Python QA and shader diagnostics.
- `build/.tmp/package-source-UG2jNf/`: all-target build, 16 affected tests plus selective module cleanup, corrupt
  cache recovery and archive lifetime across working-directory changes. CLI/Python copied-artifact and multilingual
  VPK image parity pass; source-free game shaders match authored-source output.
- `build/.tmp/player-source-amxwOo/`: all-target build and four affected regressions. VGui directory/VPK/embedded
  images match exactly, including RCSS/font/PNG/checkbox resources and failure recovery. The real Research HUD was
  composited over its scene and visually checked. Standalone native/Lua/C# batch parity passes.
- `build/.tmp/merged-roadmap/`: affected E0 builds including Research, four selected tests with two offscreen GPU
  regressions, eight generator test methods and real codegen consistency. Managed safe-value/default/ABI round
  trips and callback-phase/zero-allocation checks pass. Inspector/workspace readbacks cover held-drag updates,
  invalid-edit recovery and save/reopen parity. Focused clang-tidy, formatting and whitespace checks pass.
- `build/.tmp/cleanup-ekTVbI/`: final all-target build and nine CPU/offscreen regressions pass. Generator cleanup
  preserves checked-in outputs; eight generator methods, managed-control, 81 changed C/C++ format checks and
  AssetSource clang-tidy pass. The final test log has no unexpected GPU validation diagnostics.

These continuations did not rerun native input, Windows/D3D12 or physical XR. Earlier Windows evidence covers
MSVC 14.42/MT, GLFW/Vulkan, RTX 4080 and .NET 10.0.301: batch/workbench/Python, shader cooking/authoring and native
workbench/player smoke passed. It predates the current AssetSource/E0 work. Previous logs remain under
`build/.tmp/windows-*`; do not treat them as acceptance of these new changes or separate-machine delivery.

## Next gates

Use `docs/research_milestones.md` for the merged E0–E7/M0–M7 roadmap and `docs/future_tasks.md` for the audited
legacy feature inventory. The dev-next audit uses commit `d8fe93850d7dbeeebc6992476566d7f70bc6ca86`; it does not
justify bulk ports.

- Finish E0 object/property references, arrays, factories and generic generated ABI access. Then implement explicit
  script attachments/exports/reload state and editing history in `example-scripting`. Safe records alone do not
  complete E0 or the multi-language engine model; Undo/Redo and E1–E7 remain open.
- Complete embedded engine-resource direct reads and Lua stream loading. Base runtime exports should retain the
  single-EXE contract; C# and optional vendor-plugin dependencies require explicit delivery manifests.
- M7 remains open: VRI implements D3D12, but Vultra selects Vulkan and pinned static Slang disables DXIL. Establish
  reproducible DXIL cooking and actual Windows draw/compute/readback before exposing backend selection. Linux
  development binaries reference GLIBC_2.43; choose a release sysroot for clean-machine delivery.
- Validate current Windows AssetSource/E0, native GLFW/SDL3 input, Wayland/X11 and clean Windows/Linux deployment.
  Retain unresolved Hyprland requested-resize/detached-viewport assertions and interactive Wayland picking.
- Physical XR remains unverified. Simulator pipeline-cache/timestamp/extension diagnostics are not clean headset
  acceptance. The .NET WASM AOT probe established toolchain/ABI evidence only. Web, NVIDIA SDK passes, async compute
  and optional browser streaming need separate input, synchronization and delivery validation.
