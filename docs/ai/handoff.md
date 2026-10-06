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

### Windows continuation, 2026-10-06

Pulled `dev-VRI` from `cee4959d` to `2fe875ee` with a clean initial working tree. Current verification uses
MSVC/MT, GLFW/Vulkan, RTX 4080 SUPER and .NET 10.0.301. Logs and isolated layouts/captures are under
`build/.tmp/windows-pull-20261006-1791276446331/`; standalone scripts also print their unique output directories.

- All-target builds pass. The pulled revision passes all 43 registered xmake tests. After the local RT stage fix,
  all 13 affected shader/GPU/meshlet/reference-renderer regressions pass. These test logs have no Vulkan VUIDs.
- Standalone offline QA passes 37 invocations, including copied-executable VPK/native/Lua/C# parity. Workbench QA
  passes 15 invocations, including save/reopen, reference AOVs, lighting edits, batch parity and invalid-input
  recovery. Python checks pass owned NumPy/CLI HDR parity, closed-session/thread rules and copied-library/VPK use.
- Eight generator methods, real codegen consistency, managed safe-value/default/ABI and zero-allocation controls,
  and native Slang completion/definition/source-mapped diagnostics pass.
- All 22 supported example configurations complete six frames and produce PNGs: all basic, scene, ray and XR
  modes; UI and scripting; research Forward/deferred/data graph; and game shader cooked/edit/deferred/meshlet
  modes. Representative Workbench, Sponza meshlet, game material, ray and XR mirror images were visually checked.
  This is finite-frame coverage, not a manual interaction or headset-display acceptance test.
- A copied standalone player runs with an external VPK and with an embedded VPK, from a directory without project
  sources/build tools. Both render six frames with no warning/error/VUID diagnostics and identical 1024x768 PNGs.

Local fixes are uncommitted: Workbench QA now writes numeric `renderer.path = 2` and top-level `seed`, matching
the deliberate workspace schema change. Shared shader compilation and archive validation previously omitted all
RT stages; this broke both ray-triangle and ray-cornell despite the full registered suite passing. Compile-stage
mapping, explicit selection, save validation and archive decoding now accept all six RT stages. A regression
covers discovered/explicit entries and source-free bytecode/stage roundtrips; combined stage bits remain rejected.
Both actual ray examples now capture successfully without validation diagnostics. The generated renderer
descriptor correction contains whitespace only; current Windows codegen consistency passes.

Remaining limits:

- `example-shader --meshlets --deferred` fails explicitly: NaiveDeferred still requires indexed geometry.
  Forward meshlets and indexed deferred pass separately; mesh-driven G-buffer drawing is not implemented.
- The active runtime is **Pimax OpenXR**, not Meta XR Simulator. Both XR examples complete six application/eye
  frames and capture their mirror, but report `VUID-VkDeviceCreateInfo-pNext-02830`: an extra timeline-semaphore
  feature structure accompanies Vulkan12Features. The pinned VRI source enables timeline semaphores in
  Vulkan12Features, without a separate timeline structure; Vultra forwards that create info unchanged through
  `xrCreateVulkanDeviceKHR`. This points to XR runtime/layer insertion, but is not a proven root-cause diagnosis.
  Do not suppress the diagnostic or report clean headset acceptance. This differs from the previously deferred
  pipeline-cache issue.
- Built-in path-tracer cooking reports Slang E41012 (implicit profile upgrade). It is not a GPU validation error,
  but capability/profile declarations still need a deliberate review.
- Modified C++ files pass clang-format 18.1.8 and clang-tidy with no project diagnostics. A broader dry run of the
  81 pulled C/C++ files still reports formatting differences in 16 unchanged files. They were not bulk reformatted;
  reconcile formatter versions/outputs before claiming a clean cross-platform formatting gate.

The proposed internal replacement shader design is in `docs/shader_system_design.md`. It defines typed Boolean/
exclusive Enum keywords, Material/Pipeline/Pass ownership, link/module/preprocessor lowering, legal selections,
explicit cooking coverage and separate selection/program/layout/pipeline identities. vshadersystem is not a
dependency. Keyword domains and retention remain proposals; the scoped module/program reuse slice described
below is implemented. The existing
runnable shader guide links this design and records the indexed-deferred limitation.

Earlier Linux continuations and Windows logs do not establish D3D12, Windows SDL3, clean-machine delivery or
physical headset display/input correctness. Retain those separate gates.

### Shader compilation performance, 2026-10-06

Local uncommitted implementation and QA are under
build/.tmp/shader-performance-20261006-1791283066759/. No experimental shader-system dependency was added.

- ShaderCompiler owns one lazy Slang global session and a bounded set of 16 primary-module IR snapshots.
  Every request uses an isolated session before linking constants/modules. Native sweeps can retain a context;
  source-backed ShaderPipeline retains one across reloads. One game asset compilation shares a context across
  Passes/variants. Static convenience APIs still work; source-free cooked loading never creates a compiler.
- Both authoring paths use the same checksummed development program cache (.vultra/shaders/*.vshadercache).
  The hash only selects a file: full canonical requests, captured content hashes and ordered include resolution
  establish a hit. Hits return owned SPIR-V/reflection without Slang initialization. Failed compilation retains
  successful cache files; corruption and lookup collisions explicitly recook. Shipping .vshaderc version 1 and
  strict typed readers remain unchanged; development cache request text is not packaged.
- Device owns an in-memory VRI pipeline cache. Built-in renderer/IBL/tone mapping/reference compute, ShaderMaterial,
  TextureBlit, VGui and the direct graphics/compute examples pass it in ordinary VRI descriptors. This supplements
  existing pipeline-object reuse. No disk driver cache or RT pipeline-cache support is claimed.
- The cooker now accepts all six RT stage names as explicit --entry values. Cooking raygen/miss/closesthit from
  the real triangle source succeeds with its actual common/built-in/external include roots.

Measured on the existing Windows/RTX 4080 SUPER setup, with fresh output paths to avoid whole-asset cook skipping:

| Cooker input | Previous cold process | New cold process | Program-cache process median, five runs |
| --- | --- | --- | --- |
| PaintedMetal game asset, all generated Passes | 4.121 s | 1.804 s | 180.46 ms |
| Native built-in Forward Slang | 0.834 s | 0.585 s | 61.95 ms |

Cold figures are single observations, not statistical guarantees. Process times include startup and artifact
writing. The small compute GPU probe separately measured frontend cold 129.6 ms versus reused IR 4.0 ms, and
reopened program-cache loading 1.1 ms. These gains do not remove new specialization linking/SPIR-V costs.

Verification:

- All-target builds pass. All 44 registered xmake tests pass after the shared compiler and device-cache changes.
  After retaining the native reload context, all six affected native/GPU/compiler/meshlet tests pass again.
- New GPU checks cover native link-constant isolation/reflection, same-timestamp include edits, earlier search
  roots, macros, changed linked files, compilation failure/recovery, corrupt caches and checksum-valid lookup
  collisions. Actual Low/High game variants produce distinct expected values and survive source-free loading.
  Native compute produces the same readback with and without the device driver cache.
- Eleven finite-frame examples pass with PNG captures and no Vulkan VUIDs: basic mesh, all three ray modes,
  research Forward/deferred/data graph, and game cooked/edit/deferred/meshlet. Game cooked/edit/meshlet images
  match exactly, as do the cooked game and ray-triangle images against the previous QA captures.
- The new copied runtime renders six frames from an external VPK with only system directories on PATH and no
  project source files in its run directory. Its image matches the previous standalone player capture.
- Modified C++ code passes configured clang-tidy checks without project diagnostics. All modified C/C++ format
  checks, generated-code consistency and git whitespace checks pass.

Limits: native source reload is still synchronous. Game candidate compilation uses the existing vtask worker;
GPU creation/publication remains on the main thread. Module IR is only in memory and only reuses the primary
module; linked modules/composed groups are not separately cached. Game metadata edits conservatively invalidate
dependent programs. Typed keyword domains, Used/AllLegal retention, domain projection and selection-to-program
deduplication remain proposed gates in docs/shader_system_design.md. D3D12/DXIL and the earlier XR issues were
not validated or changed by this performance work.

### Research correctness and composition, 2026-10-06

The requested high-priority slice is implemented in the existing Vulkan research path. Logs and captures are under
`build/.tmp/research-gates-20261006-1791291146781/`. Changes remain uncommitted alongside the earlier shader performance work.

- Shared built-in shadow, skybox, G-buffer and deferred-lighting implementations now have typed PassCatalog
  contracts. `examples/research/deferred.vgraph` is a complete explicit composition. ExperimentSession and
  ResearchWorkspace bind owned renderer contexts and omit the automatic prelude for such graphs. Direct C++ can
  call the same stage builders without a SceneTree. An intermediate-only G-buffer graph culls shadows/lighting;
  rejected duplicate stages and mismatched ports retain the previous graph and completed image. The first marked
  output still obeys the existing HDR/display-color contract; additional marked outputs expose raw AOVs.
- Reference environment sampling uses exact lat-long cell solid angles and a luminance alias table with a 5%
  uniform-sphere mixture. The stored float alias probabilities determine the PDF used by both MIS paths. GPU
  upload/readback occurs only when the environment handle changes, between completed frames. Float RNG midpoints
  use 23 bits so rounding cannot produce 1. Material/environment lookup explicitly remains bilinear mip 0;
  primary jitter integrates that pointwise model. No implicit ray-cone or raster-derivative approximation was added.
- The existing libclang generator reuses scene PODs in the experiment ABI and generates frozen Python camera,
  light, environment and material dataclasses from the same reflected fields/defaults. Transform arrays convert
  between ordinary Python 4x4 indexing and the native column-major ABI. Persistent UUIDs resolve only within the
  session's SceneTree; direct-model sessions and wrong kinds fail explicitly. Numeric edits update existing GPU
  data on the next step, without geometry uploads.
- Reports share ExperimentSession provenance: the scene/graph snapshot, build mode/Slang toolchain, declared asset,
  entry-scene/environment and available shader-artifact hashes, plus actual importer-consumed model/buffer/texture
  dependency hashes. Cache hits preserve those dependencies and scene imports merge/check shared-source records.
  Reporting hashes outside measured frames. Declared file hashes are read at report time; consumed dependency
  hashes describe import-time bytes. The caller still supplies a source/build revision and retains external state.
- ImageView maps RGB, individual RGBA channels or luminance into an explicit finite increasing range without
  mutating raw floats. Workbench mapping writes a separate preview texture outside experiment timings. Pixel
  readback happens only on request after completion. Python returns owned mapped/raw arrays and raw RGBA probes.
  Blit push constants use eight scalar fields (32 bytes), checked against Slang target reflection; a vector padding
  member had introduced a 44-byte SPIR-V layout during QA and was removed. The preview explicitly transitions from
  color attachment to shader resource before ImGui sampling.

Verification on Windows/MSVC-MT/GLFW/Vulkan, RTX 4080 SUPER:

- All-target build and the final UI target build pass. All 44 registered tests pass in the final full run with no
  Vulkan VUIDs. Expected malformed-input/cache/reload fixtures retain their diagnostic coverage.
- Environment tests establish constant/black-map uniformity, normalized PDFs, bright-cell sampling frequencies and
  a known sphere integral. Independent upstream OpenPBR C++ midpoint quadrature versus GPU white furnaces covers
  diffuse, dielectric, metal and coat (largest observed channel error about 0.0012). A high-contrast filtered HDR
  fixture uses independent CPU bilinear lookup/integration (red CPU 1.14019, GPU 1.14607). Existing analytic,
  normal/material, mirror/alpha, reset/failure recovery and deterministic Cornell convergence checks remain.
- Python/CLI raster and reference HDR parity, typed edit/restore/error handling, environment changes, matrix layout,
  raw probes, mapped channels, independently computed file hashes, explicit-stage parity and copied-library/VPK
  delivery pass in `python-final.log`. Actual importer dependencies survive cold/warm cache paths in native tests.
- Workbench QA passes 17 invocations, including explicit stages followed by the same post-processing as the
  default graph, byte-identical final PNGs, save/reopen, marked AOVs, reference rendering, failed edits and batch
  parity. The actual mapped Outputs panel was visually inspected in
  `build/.tmp/workbench-qa-3q7mhjpf/lighting/workbench.png`.
- Offline QA passes 37 invocations, including copied-executable VPK and native/Lua/C# project parity with build tools
  absent from PATH. Ten generator test methods and real generated-code consistency pass. All 57 changed/new C/C++
  files pass clang-format 18.1.8; configured clang-tidy passes on the 21 affected research C++ units without project
  diagnostics, including a repeated check of the final UI fix. Git whitespace checks pass.

Scope remains the documented opaque OpenPBR subset and alpha masking on Vulkan. Game Surface functions are not
automatically ray-traced. Indexed deferred composition does not imply mesh-driven G-buffer support. Orthographic
ray origins, footprint-filtering approximations, deformation/SDK temporal signals, other material domains,
D3D12/DXIL, clean-machine deployment and physical XR/native interaction remain separate capability/acceptance gates.
Typed keyword retention and persistent driver-cache/async compilation work remain in the shader design/performance
handoff above; do not label these unimplemented features as completed merely because the current research tests pass.

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
- Current Windows GLFW/Vulkan AssetSource/E0 evidence is above. Still validate Windows SDL3, manual native input,
  Wayland/X11 and clean Windows/Linux deployment.
  Retain unresolved Hyprland requested-resize/detached-viewport assertions and interactive Wayland picking.
- Implement the internal shader design one verified gate at a time; do not import the experimental shader runtime.
- Physical XR display/input remains unverified. Retain the current Pimax feature-chain issue and earlier simulator
  pipeline-cache/timestamp/extension diagnostics; neither establishes clean headset acceptance. The .NET WASM AOT
  probe established toolchain/ABI evidence only. Web, NVIDIA SDK passes, async compute and optional browser streaming
  need separate input, synchronization and delivery validation.

### Embedding for PVW, 2026-10-06

The maintainer authorized committing/pushing the verified library changes before pinning a PVW submodule.
xmake now preserves a parent project's metadata/tooling, defaults embedded examples/tests off, exports the core
target without standalone applications/managed/game-UI targets and omits editor-canvas/RmlUi/Lua package requests.
Shader fingerprints read the library's source root while generated headers remain in the parent build directory.
Dependency versions/runtime/VRI patches are unchanged. Human instructions are in docs/guide.md.

A separate xmake parent in `build/.tmp/pvw-embedding-1791295344199/consumer` builds and executes a real VRI clear/readback.
Use `-P .` for this nested temporary fixture so xmake does not select the ancestor project. The core-only
subproject and standalone all-target build pass. PVW will pin the pushed commit; its research methods stay owned by
the maintainer, outside the library. No paper-specific algorithm or private assets are part of this library change.
