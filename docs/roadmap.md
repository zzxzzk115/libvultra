# Roadmap

[Architecture](architecture.md) defines the stable boundaries. Features accumulate around a small `vultra`
library; engine and research additions stay optional. VRI and RenderGraph remain the rendering boundary/executor.
All file and ABI versions remain `1`; breaking changes update both ends without migration code.

## Current foundation

Selected source/package/target composition, direct GPU examples, EnTT-backed CPU world, generated properties/C ABI,
Native/Lua/C# hosts and offline research paths are implemented. The current Linux migration has build, static-analysis
and offscreen/private-X11 regression evidence; physical desktop/Wayland, Windows and clean-machine acceptance
remain separate.
Implementation is not completion of every milestone. [Handoff](ai/handoff.md) records current evidence and blockers.

## Optional engine framework

Keep the existing E0–E7 sequence. The next combined slice is E0 → E1/E2 → E3 in `example-scripting`, before
expanding the editor: two instances of one script class with independent exports, typed references, live edits,
one undo action per gesture, save/reload, deferred deletion and successful/failed code replacement.

| Gate | Current boundary | Remaining work and acceptance |
| --- | --- | --- |
| E0 — types/properties | Generated values, references/arrays, inherited Node accessors and explicit static type catalogs. | General Resource/component/script-class metadata; shared factories/bindings must reject wrong, foreign and retired identities. |
| E1 — script attachments | Project-declared instances and common hosts work; Native/Lua reset local state on reload. | Scene-owned module/class/exports; one attachment per node, exports applied before entry, separate transient reload state. Two instances must save/reload independently; failed replacement preserves active code. |
| E2 — execution | Shared enter/ready/exit, fixed/variable clock, pause/step, priorities, deferred mutations and world input actions. | Signals, scene input/unhandled-input routing and seeded simulation streams. Define ordering/reentrancy and owner-scoped disconnect; all languages agree and stopped owners receive no callbacks. |
| E3 — documents | Generated Inspector and staged scene/graph editing work. | Topology/attachment authoring and undo/redo. Apply drag values immediately, merge the gesture, roll back failed composite edits and keep deleted subtrees owned by history. |
| E4 — resources/scenes | Authored snapshots, stable IDs and VPK byte sources work. | Version-1 resource documents, PackedScene templates/nesting, stable instance mappings, sparse overrides and explicit MakeUnique. Validate references/cycles before activation; finish Lua stream loading and engine shader bootstrap without extraction. |
| E5 — views/game UI | Raster synchronization, output probes and ordinary VGui controls work. | Independent RenderViews/history, object picking, RmlUi transforms/masks/layers/effects. Test input, pixels and teardown; unsupported operations fail explicitly. |
| E6 — independent Play | Offline rendering is available; browser streaming is absent. | Separate player/world with pause/step/stop/logs. Keep authored state unchanged; bounded preview defaults to 15 fps/two queued frames, drops oldest and owns cancellation. Native windows are explicit. |
| E7 — delivery | Built pack/export APIs, external/embedded VPK and copied no-xmake tools work locally. | Export-template/dependency validation and separate-machine Linux/Windows acceptance; preserve direct C++, optional languages and the base single-EXE player. |

A project owns registrations/assets/code; a world owns ECS state, clock and script instances; the editor owns
documents/history; views own their GPU frame/history. New features must respect these scopes. Native extension
types cannot unload while their objects/catalog entries remain live. C# script-instance reference restoration and
a common export schema remain unfinished; existing managed field restoration is not that schema.
Research/editor bridge tables must converge on the same generated IR rather than maintain parallel schemas.

## Graphics research

M0–M6 have a running offline path: description → scene/scripts → catalog graph → raster/reference → AOVs/reports.
Close the remaining acceptance gates on this path.

| Gate | Existing capability | Remaining acceptance |
| --- | --- | --- |
| M0 — baseline | Fixed input/camera/time step/seed, build/device/provenance and linked images/timings. | Reproducible Linux/Windows baselines and explicit external/script RNG state. |
| M1 — pass contracts | Explicit catalogs, typed ports/capabilities, C++/JSON compute parity and builtin raster contracts. | Complete authoring/inspection coverage for internal renderer stages while retaining direct `addPass()`. |
| M2 — workbench | Node canvas, numeric parameters, staged replacement, workspaces and output export. | Native interaction coverage, complete renderer-stage editing and failure/recovery after authoring changes. |
| M3 — synchronization | Cameras/lights/environments/materials and incremental transforms/constants shared by bindings. | Broader topology-authoring and script/export regression through the same object/property semantics. |
| M4 — batch/Python | Windowless project/VPK sessions, generated ABI, owned NumPy arrays and local CLI/Python HDR parity. | Cross-platform/clean-machine delivery and remaining resource-stream dependencies. |
| M5 — reference transport | Progressive opaque OpenPBR subset, MIS, ordinary AOVs, state resets and convergence fixtures. | Cross-platform baselines; footprint filtering, additional materials and deformation are distinct extensions. |
| M6 — resources/diagnostics | Explicit history, safe whole-resource reuse, hierarchical timings and linked RenderDoc captures. | Replay/Nsight inspection and broader invalidation coverage. Async compute requires verified VRI queues/synchronization first. |
| M7 — backend/delivery | Vulkan/SPIR-V delivery; pinned VRI also has D3D12. | Vultra D3D12 selection, DXIL cooking and scene/graph image parity on Windows hardware. Current Vulkan results do not validate D3D12. |

“Research workflow comparable” requires M0–M6 acceptance; backend breadth additionally requires M7.
Current image fixtures establish their declared subset and same-device behavior, not complete Falcor/OpenPBR parity.

## Shader system

Retain native Slang and game materials. The next design adds finite Boolean/Enum keywords with explicit
Material/Pipeline/Pass ownership, legal-selection validation and named presets. Keep selection, compiled program,
layout and pipeline identities distinct. Cooking retains used or all-legal combinations with an explicit policy;
equivalent selections deduplicate programs. Dynamic values remain properties/uniforms, with no global keywords.
Accept typed diagnostics, deterministic cooking, packaged coverage and last-valid reload before expanding editor UI.
This replaces neither Slang nor the current explicit pass/graph path; keyword planning is not yet implemented.

## Optional extensions

Vendor passes are independent optional targets explicitly installed into the caller's pass catalog.
SDK headers, native interop and dependencies stay outside the core and script ABI.

- NRD: consume defined diffuse/specular AOVs, then shadow signals; test history/resize and plugin-free builds.
- DLSS/DLAA: settle jitter/motion/depth/exposure/extents and verify VRI/Streamline interop before pass integration.
  Required vendor DLLs are explicit export dependencies; missing files fail. RTXDI/Ray Reconstruction/Frame
  Generation remain separate projects.
- Physics, animation and audio: optional ECS/server adapters with fixed-step/transform and resource-retirement
  contracts. Jolt, Ozz and miniaudio are candidates, not new foundation dependencies.
- Simulation reset/actions/observations, Python simulation, material graphs and Gaussian splatting: implement
  concrete data/image regressions before tooling. Python remains an external optional client.
- Web: separate VRI WebGPU, WGSL, browser frame/package/script delivery work. Existing desktop packaging and
  .NET WASM/AOT probes are not a working Web player.

The inspected [dev-next revision](https://github.com/zzxzzk115/libvultra/tree/d8fe93850d7dbeeebc6992476566d7f70bc6ca86)
provides behavior references for VFS/export, bounded video preview, simulation, material graphs, animation,
physics, audio and Web templates. Redesign around current ownership; do not copy its service locator/RHI/graph.
Vendor references: [NRD](https://github.com/NVIDIA-RTX/NRD),
[Streamline](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md).

Every gate needs affected builds/tests, codegen consistency, formatting/static analysis and failure/recovery coverage.
Rendering additionally needs finite GPU diagnostics and image readback. Prefer offline QA; visible testing uses
Workspace 5. Build commands are in [BUILD.md](../BUILD.md); machine/work-state evidence stays in the handoff.
