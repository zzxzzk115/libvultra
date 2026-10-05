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
- `vultra-pack` exports external or EXE-embedded project VPKs. Built-in SPIR-V programs and OpenPBR attribution are embedded separately.
  Source examples retain Slang/FileWatch; packaged built-ins load cooked programs without watchers. Archives currently extract to temporary files. Lua is static; native modules are files and C# needs .NET/host files.
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

## Windows verification: 2026-10-05

Fast-forwarded `dev-VRI` from `5d4305f8` to `65768618`. This run uses Windows x64 / MSVC 14.42 / release / MT,
GLFW, Vulkan, an RTX 4080 SUPER and .NET SDK 10.0.301. SDL3 and Linux were not rerun for these changes.

- The initial all-target build failed because ResearchWorkspace default-initialized a span of forward-declared
  PassTiming values. Its header now includes the complete profiler declaration directly; all-target build passes.
- `xmake test -y -v -j1` passes 32/32, including native GLFW/window/viewports, offscreen input/GUI, scene/script GPU
  synchronization, graph editor/Inspector persistence, graph history/transient reuse and reference transport.
  Expected invalid shader/plugin/cache inputs retain diagnostics and recovery assertions.
- The standalone batch QA passes direct C++/JSON graph parity, invalid-input recovery, fixed-step scripts and copied
  EXE/VPK startup without build tools in the child PATH. PNG/PFM outputs match for unpackaged and packaged projects,
  including native/Lua/C# fixtures. These copied-artifact checks use the development machine and installed .NET.
- Workbench QA passes raster/reference save/reopen, explicit display composition without a second tone map, marked
  outputs, environment edits, rejected-document recovery and exact batch PNG/PFM parity. Raster and reference UI
  captures were inspected. This is the real offscreen GUI, not manual desktop interaction.
- Python QA passes experiment-description replay/rejection recovery, three numeric scans with exact CLI HDR
  parity, reference radiance/seven AOVs and report export, creating-thread enforcement, closed-handle rejection and
  owned NumPy arrays. A copied research DLL and VPK reproduce the preceding image without repository shaders.
- The native GLFW workbench completes 30 frames with layout persistence disabled. Its exported PNG/PFM images
  exactly match the offscreen workbench baseline, and its final scene image was inspected. No manual pointer,
  keyboard, picker or panel interaction was performed in this native workbench run.
- Codegen now normalizes source/header separators and reads MSVC compilation commands in libclang's cl mode with
  the CRT's builtin offsetof. The real `scripts/codegen.py --check` confirms all generated files are unchanged.
  Five generator tests pass, including real GNU/MSVC-command reflection parity. Python tooling is isolated under
  ignored `build/.tmp/windows-codegen-python`; the machine's Store Python aliases are not usable.
- C++ formatting and focused clang-tidy on ResearchWorkspace pass. Safe managed-control and the explicit managed
  replacement regression pass, including instance isolation, private-field continuity and failed-image recovery.

Logs use `build/.tmp/windows-*-20261005*.log`. Batch and workbench outputs are under `build/.tmp/offline-qa-vln0_w68/`
and `build/.tmp/workbench-qa-7roppjkd/`; Python outputs are under `build/.tmp/python-qa-ynrf6bgl/`, and the native
workbench export is `build/.tmp/windows-native-workbench-20261005/`. Successful rendering logs have no unexpected
Vulkan validation diagnostics. RenderDoc/Nsight, current SDL3, all individual example modes, external/embedded player
exports and separate-machine delivery were not rerun in this continuation. No D3D12 or physical XR validation is claimed.

## Offline continuation: shader cooking, 2026-10-05

Implemented the shader-cooking portion of delivery without enabling an unverified backend. No desktop or XR
window was opened in this continuation; no dependency downloads or network fetches were performed.

- `ShaderProgram` owns reflected entry names/stages and aligned SPIR-V words. Source hot reload and cooking use
  one Slang implementation. Empty entry selection discovers all annotated entries, including included vertices.
  The version-1 `.vshaderc` format uses a checksummed CBOR payload with explicit target and ray-query requirement;
  readers reject corrupt payloads, unknown versions/targets, duplicate entries, oversized stages and misalignment.
- `vultra-shader` cooks without a device/display and publishes only after successful compilation. The CLI's actual
  failed-compilation/recovery probe retains the preceding artifact and reproduces the same bytes on recovery.
- `ShaderPipeline` loads explicit `.vshaderc` files or a cooked sibling when a requested `.slang` source is absent.
  Present sources continue compiling/watching. Cooked programs have no FileWatch instance; explicit replacement
  failures retain the preceding GPU pipeline. Do not introduce source compilation as a corrupt-bytecode fallback.
- `VpkArchive::packBuiltins()` cooks every built-in pass, including reference, mesh/task and VGui, and ships only
  `.vshaderc` programs plus OpenPBR license/integration notices. The shared xmake dependency tracks input files,
  the packer and configuration. A subsequent unchanged batch build skips cooking and completes in 0.219 seconds.
- All targets build. The new source-free triangle regression passes exact GPU pixels, entry/stage validation,
  malformed metadata and cooked replacement failure/recovery. The selected offline suite passes 5/5: GPU/offline,
  meshlets/default, reference-path-tracer, rendering and VPK. `test-meshlets/cooked` additionally passes the same
  indexed/task-mesh HDR, normals, culling and dev-palette assertions from the source-free embedded shader archive.
- Batch, offscreen workbench and Python QA pass with cooked built-ins, including copied EXE/DLL/VPK startup,
  scripts, graph persistence/rejection recovery and reference AOVs. Compared with the preceding Windows source
  compilation run, 47 batch, 82 workbench scene/AOV and 32 Python PNG/PFM artifacts are byte-identical. GUI screen
  captures contain changing diagnostics/timings and are not compared byte-for-byte.
- Focused clang-tidy and clang-format pass; the real generated-API check remains unchanged. Cooking preserves
  Slang's profile/capability warnings instead of hiding them. Successful standalone render logs contain no
  unexpected GPU diagnostics. The deliberately corrupt shader test emits its expected checksum error.

Logs are `build/.tmp/windows-shader-*20261005.log`, `windows-cooked-*20261005.log` and
`build/.tmp/shader-cooking-cli/cli.log`. QA outputs are `offline-qa-w1aaifdf`, `workbench-qa-6wyjd5v2` and
`python-qa-sle27x1n` under `build/.tmp/`. Existing Windows validation/codegen fixes remain uncommitted alongside
this implementation. Generated resource line-ending rewrites were restored after checking content equality.

DXIL remains disabled in the pinned static Slang recipe and VRI still selects Vulkan. Upstream Slang's pinned
`cmake/FetchDXC.cmake` supplies DXC release `v1.9.2602` with a fixed archive hash, but enabling it would fetch that
payload; SDK DXC files on this machine are not a reproducible shipped dependency. Next settle package-owned DXC
and DXIL cooking, then real VRI D3D12 draw/compute/readback before exposing backend selection. The cooking format
remains version 1 during any deliberate breaking change. At this cooking-only checkpoint, arbitrary project shader delivery and the embedded color-gain source still
needed explicit cooking integration. The shader-authoring implementation below supersedes that gap; Slang remains
linked for source editing and native project compilation.
No Linux, native input, current external/embedded desktop player or physical-XR acceptance was rerun here.

## Shader authoring implementation, 2026-10-05

The requested four implementation waves are present. This section supersedes the cooking-only checkpoint above.
Human-facing syntax, ownership, VRI contracts and commands are in `docs/shader_system.md`.

- **Language and format:** game `.vshader` is text; native `.slang` remains independent. Both cook to a checksummed,
  explicitly typed version-1 `.vshaderc`. Readers, writers, packers, builtins and callers use the new extension,
  without guessing an old format. ANTLR generator/runtime are pinned to 4.13.2 with MT/MTd; generated C++ is checked
  in and private. Normal builds need no Java. Lexer modes protect comments, quoted/character/raw strings and
  continued preprocessor lines; raw string delimiters must match. Source diagnostics and generated projections
  retain original locations. CPU/parser/source maps have rejection and boundary regression tests.
- **Parameters and assets:** typed Properties, attributes/defaults, explicit variants, Slang link modules/constants,
  ShaderAsset and MaterialInstance are implemented. Slang's SPIR-V reflection owns offsets, arrays, matrix major
  order/stride and resource sets. Cooking caches input/options/toolchain/template identities and actual dependency
  bytes; changed include resolution invalidates the cache. Failed cooking retains the last successful artifact.
  Project VPK cooking keeps AssetIds while replacing explicit shader sources with cooked assets. Game/raw loaders
  reject the wrong source kind. Typed scene materials preserve the existing numeric OpenPBR category.
- **Builtin raster integration:** one OpenPBR Surface function supplies Forward, both G-buffer groups, DepthOnly,
  ShadowCaster and task/mesh Forward. Shared templates handle normals, alpha mask, mirrored transforms and faces;
  mesh entries reuse the existing meshlet implementation. Explicit Passes own stages and inherited VRI state;
  attachment/resource scheduling belongs to the host graph. SubShader selection records feature/mode/host-contract
  rejection reasons. The builtin renderer validates output counts and its 32-byte draw ABI. State-property edits
  select cached pipelines. Scene materials and the direct example retain their existing geometry.
- **Workflow:** scoped ShaderRuntime uses vtask for isolated compile candidates and the main thread for GPU
  preparation. It publishes all instances at a completed GPU boundary, rejects stale source/dependency revisions,
  preserves matching overrides, reports default additions/removals/type changes and retains the old asset on
  failure. Removed instance indices remain stable/reusable. Generated directories are excluded from FileWatch.
  ResearchWorkspace and the packaged player prepare GPU objects before recording. RuntimeApp uses the same
  SceneShaderMaterials binding path and destroys borrowing renderers before their contexts. Its scene graph is
  prepared independently of the acquired backbuffer; command recording copies scene output and draws game/plugin
  UI. The workbench scene Inspector and direct example share the shader material Inspector; shader diagnostics are visible in the workbench. ShaderPipeline retains its short
  native interface and also accepts full ShaderCompileOptions.
- **Texture resources:** public lightweight texture loading/upload preserves authored DDS mip chains, arrays,
  volumes and cube faces. Color/data/normal usage is checked. Default textures cover all five dimensions. The local,
  checksum-verified VRI cube-array patch enables the already queried Vulkan imageCubeArray feature; the package
  README explains rebuilding an earlier local package installation. No backend/version change was introduced.
- **Editor services:** `tools/vscode_vshader` provides outer highlighting, compiler diagnostics, generated Slang
  viewing and mapped completion/definition/hover through the official Slang extension. Readonly generated property
  declarations cannot receive completion edits. Unsaved source and stale document versions are handled explicitly.
  Editor setup merges include roots into both extension configurations, discovers game source folders and preserves
  personal settings/colors. It does not install or publish the extension.

Verification on Windows/GLFW/Vulkan includes the eight shader test targets, raw GPU draw/readback and failure
recovery, native compute graph/session tests, native meshlets, the RayQuery reference renderer, rendering,
research workspace, numeric scene Inspector and VPK. Shader Surface readbacks exercise indexed/mesh/deferred
parity, independent DepthOnly, shadow mask, OpenPBR channels, RGBA normal mapping, mirrors/backfaces and dynamic
Cull pipeline reuse. Layout tests check matrix padding/major strides and descriptor arrays/sparse sets. Typed DDS
textures are actually sampled on the GPU. Inspector tests send real pointer events for edits/reset/variants.
Game VPK tests remove source before startup and compare exact HDR output. Current Windows CLI acceptance cooks
a game project, deletes its shader source and runs external and embedded players for five frames; their PNG bytes
match and both logs are free of unexpected VRI validation errors. The latest workbench loads cooked game materials
in a finite offscreen run and exports images. The direct game example runs finite Forward/deferred/mesh/source-edit
modes with isolated layouts and PNG captures. Expected corrupt-bytecode and
shader-syntax tests retain their diagnostics; clean rendering logs must contain no unexpected VRI validation errors.
A scoped VRI CreateComputePipeline failure injection rejects the second candidate after the first succeeds and
checks that both old bindings/generation and subsequent GPU output remain intact. The final check summary is
`build/.tmp/shader-checks/final-acceptance.json`; earlier probe logs retain intermediate failures and fixes.

The native Slang language-server test checks actual completion, readonly property navigation, relative include
navigation and mapped error diagnostics. Full interactive VS Code Extension Host behavior has not been manually
validated. PowerShell parser regeneration and the editor setup merge/idempotence probe were exercised; the Unix
end-to-end scripts and Linux/SDL3/hardware XR were not run here. OpenPBR remains the existing opaque base/specular/coat subset;
Surface functions are not automatically ray/path-traced. No D3D12, DXIL, Web or separate-machine acceptance is
claimed. Ordinary shader editing still links the compiler; cooked startup does not parse/compile source or need
external ANTLR/Java components.

Verification logs, captures and summaries are under `build/.tmp/shader-checks/`. The 64 modified owned C++ files
pass clang-format; all 45 modified translation units pass the configured clang-tidy rules. ANTLR-generated C++
is excluded. This Visual Studio tidy installation lacks its resource headers: verification supplies the matching upstream LLVM 18.1.8 cpuid header in the ignored check
folder and the CRT's builtin offsetof definition. These are verification-environment fixes, not repository dependency
changes or diagnostic suppressions. Existing Windows/codegen fixes are preserved in a separate build-fix commit.

## Publication cleanup, 2026-10-05

The work is grouped into English commits: Windows build/codegen fixes (`24fa84b`), shader/material/cooking
implementation (`9f03a58`), editor/player/example services (`1872653`), and the accompanying documentation.
Personal editor settings, layouts, imported caches, parser-generator metadata and verification outputs remain
ignored. Generated C++ parser sources are intentionally tracked; normal builds still need no Java.

The cleanup pass makes both regeneration scripts normalize LF endings, trailing whitespace and generated
indentation without changing parser logic. PowerShell regeneration, sh syntax validation and the actual Unix awk
normalizer (under Git for Windows) pass; all four normalized files match byte-for-byte. Full Unix regeneration
was not completed because this minimal shell bundle lacks SHA-256 command-line tools. No Linux acceptance is
inferred. The rebuilt shader-authoring and native shader-program GPU regressions pass, as do five codegen tests
and the real generated-API consistency check. Owned C++ formatting and staged whitespace checks are clean.

## Remaining gates

Read `docs/research_milestones.md` and `docs/future_tasks.md` before selecting work. The latter audits actual sources
at dev-next commit `d8fe93850d7dbeeebc6992476566d7f70bc6ca86`; it does not authorize bulk ports. Resource streams,
DXIL cooking, export templates and an optional offline browser stream precede larger engine features.

M7 is open: pinned VRI already implements D3D12, but Vultra selects Vulkan and static Slang disables DXIL. Settle
working DXIL cooking/compilation and validate on Windows hardware before exposing backend selection. Separate clean
Linux/Windows delivery, Linux native GLFW/SDL3 input, Wayland/X11 and current Windows SDL3 acceptance remain.
The current Windows GLFW/Vulkan automation is recorded above;
it does not establish SDL3, manual workbench interaction, D3D12 or separate-machine delivery acceptance. Current
Windows external/embedded player acceptance for game shaders is recorded in the shader-authoring section above;
other earlier player smoke runs predate these research changes.

Retain previous unresolved Hyprland requested-resize/detached-viewport assertions and interactive Wayland picking.
Physical XR is unverified. Prior simulator logs include pipeline-cache validation, missing timestamps/timeline
emulation and unsupported extensions; do not classify simulator smoke as clean hardware validation. A previous
.NET WASM AOT probe was only toolchain/ABI evidence, not a Web player. NVIDIA SDK passes, async compute and Web
remain separate work with explicit input/synchronization/delivery requirements.
