# Vultra Unified Roadmap: Graphics Research and Editable Multilanguage Core

The target combines a reproducible graphics research workflow with an editable, multilanguage engine core.
Direct C++ programs, the workbench and the player use the same scene data, properties, script objects, pass
contracts and RenderGraph. VRI remains the only graphics abstraction, RenderGraph remains the only executor,
and `vultra` remains the public static library. The graphics capability reference is
[Falcor's workflow](https://github.com/NVIDIAGameWorks/Falcor/blob/master/docs/getting-started.md).
The object, authoring and lifecycle references are Godot and
[Mainframe at the inspected revision](https://github.com/Mainframe-Games/mainframe-engine/tree/2fcdb6c4f4d1760f0aab60bd1f687574b252f293).
These references inform contracts, not an import of their implementations or dependency stacks.

## Editable engine core: implementation order

The existing M0–M7 gates below remain open until their stated acceptance passes. E0–E7 extend that same
workflow; they do not replace the renderer or require a scene tree for direct drawing. JSON remains the
serialization format for scenes, resources, projects, graphs and workspaces. Every format and ABI remains
version `1` before release; change both ends deliberately without migration readers.

| Stage | Implementation | Acceptance |
| --- | --- | --- |
| E0: properties and types | Extend the existing libclang IR with typed values, generated accessors, serialization/Inspector/binding/reload flags and explicit context-owned type catalogs. Inspector and JSON use the same descriptors; setters retain semantic validation. | Camera, light, environment, material and renderer settings use real descriptors; named reads/writes, defaults, invalid input and JSON round trips are tested. Generated ABI and safe language wrappers consume the same IR. |
| E1: script attachments | A project module catalog owns code; each SceneTree host owns instances. Each node has at most one attachment with class, module and export values. Different nodes can use different languages. Exports are applied before entering the tree. | Two instances of the same class have independent editable/saved exports. Native, Lua and C# use the same attachment/property semantics. Reload retains exports and explicitly marked state; invalid replacements retain the last valid code. |
| E2: tree execution | Parent-first enter, children-first ready/exit, fixed update (default 1/60 s), process, pause, input/unhandled input, queued structural changes and typed signals. Subscriptions belong to their node/module/UI owner. | All languages observe the same phase ordering and object identity. Removed nodes and stopped modules cannot receive callbacks; mutations during callbacks and failed callbacks have deterministic behavior. UI construction stays in the GUI phase. |
| E3: editable documents | Scene topology and attachment authoring, generic Inspector, context-owned drawers, resource inspection and undo/redo. Apply drag values immediately and merge one gesture into one history action. Removed subtrees are owned by history until discarded. | Live image changes while dragging; one undo restores the original value. Save/reopen preserves scene state. Failed composite edits undo partial work and do not enter history. No history entry retains pointers into unloaded plugin code. |
| E4: scenes and resources | PackedScene instantiation, nested scene instances, stable template/instance ID mappings and property overrides, version-1 `.vres`, explicit resource uniqueness and dependency/cycle checks. Finish Lua resource-stream loading and embedded engine shader bootstrap. | Two instances isolate mutable state and preserve stable overrides across save/reload. Missing dependencies/cycles fail clearly. Embedded/external VPK runs do not extract the entire project or need xmake. |
| E5: views and game UI | Share uploaded scene data across independent RenderViews (camera, graph, history, outputs); add picking/AOV pixel probes. VGUI owns documents, binding and input; implement transforms, masks, layers and current RmlUi effects through VRI. | Independent views do not reset each other's history. Picking resolves a valid scene object. UI interaction, clipping, transforms, filters and teardown have offline image/input tests. Unsupported effects cannot silently succeed. |
| E6: independent Play | Play launches a separate player, using the same tree execution. Default to offscreen browser preview with bounded readback/encoding, status/control protocol version 1 and pause/step/stop/logs. Native windows are explicit. | The editor document is unchanged by play. Commands enter the owning thread through a queue. Preview defaults to 15 fps and two queued frames, dropping the oldest. The player stops and releases resources deterministically. |
| E7: delivery and remaining graphics gates | Close remaining M0–M7 acceptance, export-template/dependency validation and no-xmake delivery. Preserve direct C++, offline tools, optional scripting and single-EXE base-player paths. | Separate Linux/Windows machines validate published tools/player, external/embedded VPK and supported backends. Hardware/platform limitations remain explicit until tested. |

The first combined slice is E0 → E1 → E3 in `example-scripting`: two nodes using the same script class with
different exports, immediate Inspector edits with one undo per gesture, save/reload, and successful/failed
hot replacement. Build the contracts before expanding the workbench UI.

Native C++, Lua and C# share lifecycle and identity rather than three object models. C# public scripts remain
safe; unsafe interop stays internal. A project owns one collectible managed load context and its private
dependencies. Successful replacement does not rerun ready callbacks; stop subscriptions, drawers and borrowed
callbacks before unloading code. Native and managed sidecars keep their explicit delivery requirements.

Core property descriptors do not depend on editor code. Factories and type catalogs are explicit, never global.
Runtime ObjectId, server RID and graph resource handles never become persisted references. Scene instances use
stable node/asset IDs; project assets have one owner and borrowed immutable views. Editable embedded resources
belong to the scene, with explicit MakeUnique for external resources. No ECS or second authoritative world is added.

RmlUi remains at the pinned version. Browser preview may add fixed MIT `cpp-httplib` **v0.59.0** only when E6 is
implemented, privately in tools/player; it does not enter public core headers. Physics (Jolt), animation (Ozz),
audio (miniaudio), simulation reset/actions/replay, material graphs and further research passes follow this core
as separately verified additions. Python simulation, .NET AOT and Web each need their own delivery tests.

## Current implementation

E0 has its first running property slice: generated settings descriptors live in `assets`, `scene` and `servers`;
core owns typed values/catalogs/JSON decoding; Inspector, scene settings persistence and complete renderer workspace
settings consume them. Safe C# settings records/defaults/ABI conversion also use the same IR. Node/Resource factories,
persistent reference/array descriptors, general property ABI access and script-class metadata are still open. E1–E7
remain implementation work; the first combined scripting/undo acceptance has not passed.

The first M0/M1 vertical slice connects an example-owned compute pass to the built-in renderer's linear scene color. Direct C++ construction and a saved version-1 `.vgraph` definition produce identical float GPU readbacks. Research runs either form before tone mapping, and its benchmark report associates graph parameters, source/shader/asset identifiers, device information, per-pass measurements and a final capture path. The usage and ownership contract are in the [development guide](guide.md#rendergraph).

`RuntimeContext` owns an explicit `PassCatalog`. A direct C++ program can own one without creating a desktop context. Definitions currently support texture/buffer ports, exact optional formats, matching extents, required VRI feature bits and bounded numeric scalar parameters. Graph connections determine construction order; the existing graph validates accesses, initialization and barriers. `vultra.tone_mapping` is an installed built-in contract sharing the raster renderer's implementation. It can follow the project compute pass in C++ or JSON without double tone mapping; further internal renderer stages still need catalog adoption.

## Ordered acceptance gates

| Stage | Required behavior | Current boundary |
| --- | --- | --- |
| M0: experiment baseline | Fixed scene, camera, time step and seed; version-1 descriptions associate images, timing, build and device capabilities. Linux and Windows rerun the same experiment. | Version-1 `.vexperiment` fixes input/camera/graph, seed, time step and warmup/measured frames. CLI and Python replay the same description with exact local HDR parity. Hashes, captures, device data and timing reports are available. Script RNG determinism and Linux/Windows cross-platform baselines remain caller/acceptance gates. |
| M1: pass and graph contract | Explicit context catalog; typed ports, parameters and capabilities; invalid connections fail before GPU recording. Project and built-in passes compose through C++ or graph definitions. | Project HDR compute and the shared built-in ToneMappingPass compose through C++ and `.vgraph`, with readback parity and pre-recording type/extent/capability/error tests. Numeric live parameters preserve GPU objects. Incremental shadow/G-buffer/lighting contract adoption remains. Direct `addPass()` remains available. |
| M2: graph definition and workbench | Version-1 `.vgraph` save/load; a minimal `vultra-app` selects scenes, edits parameters and connections, previews marked outputs, and exports PNG/linear HDR. Failed edits keep the last valid graph. | Implemented for static `.vproject` scenes and numeric pass parameters. `.vworkspace` saves project, graph, camera, renderer settings and extent. A node canvas provides typed wires, pass parameters, output marking, pan/zoom and saved node positions. Candidate replacement, PNG/PFM capture and the same UI's offline frontend are available. Windows GLFW regressions and native startup pass; manual desktop interaction and current SDL3 acceptance remain unverified. |
| M3: scene synchronization | Cameras, directional/point/spot lights, environments and persistent material parameters; changes update GPU data without geometry reupload. Generated ABI and script wrappers share object/property semantics. | Implemented for the current node/resource types: persistent state, common language bindings, independent revision consumers, live GPU constants/transforms and safe HDR replacement. The workbench Inspector selects nodes/resources and uses generated camera/light/environment/material property descriptions. Held-drag readbacks, rejected edits and saved-image parity are covered offline. Windows GLFW/offscreen Inspector regressions pass; manual native interaction remains outstanding. The Inspector edits existing objects rather than providing topology-authoring controls. |
| M4: batch and Python | Windowless `vultra-batch` loads the same project/VPK, scene and graph; deterministic HDR/AOV/report output. Optional generated-ABI Python/NumPy entry runs equivalent scans. | The shared C++ experiment session reads VPK CPU assets directly through a context-owned AssetSource. Fixed-step native/Lua/C# updates, the generated version-1 session API and optional safe Python/NumPy host are running; script files and native/managed sidecars are materialized selectively. Three numeric parameter points have exact Python/CLI HDR parity; copied-library/VPK loading, owned arrays and failed-edit recovery are tested. Python exports selected PFM images with device/graph/producer/resource/timing reports; reference seed/AOV parity and experiment-description replay are covered. Typed Python scene mutation, full Python build/asset provenance and cross-platform acceptance remain. |
| M5: reference path tracer | Progressive unbiased VRI ray tracing, direct light sampling and explicit accumulation reset. Radiance, albedo, normal, depth, motion and sample/ray counts are graph outputs. | A progressive opaque OpenPBR VRI ray-query path now runs in batch, workbench and Python, with seven diagnostic AOVs, analytic/emissive/environment sampling and explicit state/shader history resets. Textured/alpha/mirrored/analytic fixtures and deterministic Cornell convergence pass. Filtering/material breadth, deformation/SDK motion conventions and cross-platform acceptance remain; see [reference limits](reference_renderer.md). |
| M6: resources and diagnostics | Explicit history, safe transient lifetimes/aliasing, hierarchical CPU/GPU events and image/pass/capture association. | Explicit texture history, reference reset integration and exact-description transient reuse have GPU parity/reset tests. Nested CPU/GPU events, actual VRI allocation reports and image/resource/producer associations are exported. An actual offscreen RenderDoc capture is linked into the report. Replay/Nsight inspection and second-backend acceptance remain; async compute requires validated VRI multi-queue synchronization. |
| M7: backend and delivery | Windows D3D12 through VRI, equivalent scene/graph outputs, explicit missing-capability errors, clean Linux/Windows builds and tool/player delivery. | Built-in shaders are cooked to source-free version-1 SPIR-V programs for embedded VPK delivery. Vulkan remains the Vultra backend. Pinned VRI already contains D3D12, but Vultra selects Vulkan and static Slang disables DXIL. Working DXIL delivery and a Windows device are prerequisites. An isolated Fedora userland passes batch/player startup, raster/native/Lua VPK parity and reference AOV checks with explicit host driver mounts; separate-machine/native-window delivery remains unverified. Web is a separate project. |

The M0–M6 research path now runs offline on Linux and Windows Vulkan: experiment description → scene/scripts →
catalog graph → raster/reference rendering → AOVs and linked reports. Windows GLFW native startup and the
32-test suite also pass; see [verification limits](guide.md#verification). This does not complete all cross-platform
gates or establish Falcor feature parity. Keep the existing graph/scene failure-recovery and saved-image tests;
manual workbench interaction, current SDL3, D3D12, cross-device baselines and clean-machine acceptance remain open.
[Follow-up tasks](future_tasks.md) include
concrete remaining milestone work and a source-verified dev-next comparison.

## Resource and delivery contracts

Graph resources are imports, graph-owned transients, explicit history or exports. Graph-owned textures persist until graph destruction. Explicit history restores its initial value before first use and after `resetHistory()`. Opt-in transient reuse shares the same VRI resource only for identical descriptions with disjoint live intervals; imports, exports and history are excluded, and an aliased plan is immutable. Renderer invalidation must reset relevant size, scene, camera or shader changes. Temporary graph handles never enter scene files or scripts.

All serialized formats and the C ABI stay at version `1` before release. Change readers, writers, examples and tests together; do not add migrations. Script languages may configure scenes, graphs and experiment parameters; GPU pass recording remains C++/Slang.

The base `vultra-runtime + VPK` player remains a single executable. Optional NVIDIA integration uses independently built pass plugins explicitly installed into the current catalog. SDK headers and native interop stay outside `vultra` and the script ABI; plugins use VRI resource states and declared interop rather than interpreting VRI handles as native Vulkan objects.

| Optional plugin | Prerequisites and acceptance |
| --- | --- |
| NRD REBLUR/RELAX, then SIGMA | M5 signal/AOV contracts; history/resize tests, image comparisons and a build without the plugin. Prefer the SDK's static-library option. |
| DLSS Super Resolution / DLAA | Defined jitter, motion, depth, exposure and input/output sizes; M6 history reset; per-device support queries and independent Streamline/VRI interop verification. |
| RTXDI / Ray Reconstruction / Frame Generation | Separate projects after their input, synchronization and image tests exist; outside the core acceptance gates. |

A DLSS-enabled distribution may include required vendor DLLs. The export API must collect and validate them without xmake, list them in its manifest and fail clearly if they are missing. It must not silently substitute another algorithm. See upstream [NRD](https://github.com/NVIDIA-RTX/NRD) and [Streamline's distribution guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md).

Every gate needs affected builds/tests, generated-code consistency, formatting and static analysis. Rendering gates also need finite-frame GPU diagnostics and image readback; persistence and replacement need failure/recovery tests. Prefer offline rendering for AI/QA; use Workspace 5 only for visible Linux window/input tests. A local development-machine run does not establish clean-machine or Windows acceptance.
