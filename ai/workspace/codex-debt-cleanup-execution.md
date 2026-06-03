# Codex-Debt Cleanup Execution

Date: 2026-06-03
Status: In-Progress

## Summary

First execution pass over the remediation roadmap in
`ai/workspace/codex-debt-review-2026-06-03.md`. Covers all P0/P1 items and the safe,
build-independent parts of P2. C++ edits are written but **not yet compiled here** (the
local `vshadersystem` package fetch failed until `xmake repo -u`; the user is running
the build). Verify with `xmake build -y vultra-app` before committing.

## Result

Done this round:

- **P0-1** MCP dispatch de-dup: replaced ~17 copy-pasted `vultra.scene.*` branches in
  `runtime_mcp_tools.cpp` with one `{toolName -> editorCommandName}` table +
  `dispatchEditorCommand()`. `command`/`command_batch` stay special.
- **P0-2** Deferred-call deadline: added shared `kMcpToolDeadline` + `PendingCall::
  deferDeadline` (set at enqueue) in `runtime_mcp_server_internal.hpp`;
  `executePending` now fails+dequeues expired calls without re-running them
  (no orphaned mutation, no infinite re-spin). Removed the dead duplicate `kToolTimeout`
  in `runtime_mcp_server.cpp`; the HTTP wait now uses the same shared constant.
- **P0-9** Temp cleanup: added `tools/clean-mcp-tmp.ps1` + `.sh` (dry-run/all) and a
  "Temp Artifacts and Cleanup" section in `AGENTS.md`.
- **P1-5** Workspace hygiene: added `ai/workspace/TEMPLATE.md`, rewrote
  `ai/workspace/README.md` into a full categorized index, moved 65 completed logs to
  `ai/workspace/archive/` (every archived file is linked from the index).
- **P1-8** Knowledge hardcoding: removed the hardcoded `clang-format.exe` path from
  `vscode-workspace.md` (rely on PATH); split `ai-runtime-rpc.md` into "Implemented
  today" vs "Planned direction"; added deprecation/migration markers for
  `typeId`/`nodeId` vs `type`/`node` in `vultra-formats.md`.
- **P1-3** (partial) MCP schema casing: fixed `vultra.scene.component_metadata` to
  advertise only the canonical snake_case `component_kind` (was 3 aliases). Documented
  the snake_case wire convention in `ai/knowledge/harness-config.md`.
- **P1-6** Skill/agent metadata: enriched the three `ai/skills/*/agents/openai.yaml`
  with prerequisites/in_scope/out_of_scope/linked knowledge/verification; rewrote
  `ai/agents/README.md` with role selection criteria + interaction model.
- **P1-7** Harness config/decision tree: added `ai/knowledge/harness-config.md`
  (single source for host/port/build target, decision tree, task prioritization,
  canonical smoke set) and referenced it from `AGENTS.md` + knowledge README.
- **P1-10** RAII: converted raw `new`/`delete` to `unique_ptr` in
  `frame_debugger_system`, `xr_common_action`, and `depth_hzb_feature`.
- **P2-12 / P2-11 / P2-13**: assert policy + RAII + god-file rules in
  `ai/knowledge/cpp-conventions.md`; build-ready split plans in
  `doc/architecture/render-system.md` and `doc/architecture/asset-system.md`.

## Corrections to the audit (verified against source)

- **physics_system.cpp `new Job`/`delete job` is NOT a defect.** It is the JoltPhysics
  `JobSystem` allocation contract (Jolt owns the ref-counted `Job` and calls `FreeJob`
  back). Left unchanged; documented in `cpp-conventions.md`.
- **buffer.cpp `assert(m_Impl)` is correct.** These guard a programming invariant
  (use of a moved-from/empty `Buffer`); `operator bool()` is the recoverable check. No
  conversion. This matches the audit's own "keep assert for invariants" rule.
- The audit suggested **camelCase** for tool args, but the codebase de-facto convention
  is **snake_case** (`component_kind`, `entity_kind`, `scene.add_component`).
  Standardized on snake_case to avoid breaking the wire API.
- God files are larger than stated: `render_system.cpp` ~5048 and `asset_system.cpp`
  ~3026 lines. `MaterialParamsPBRMR` is duplicated across 4 TUs — extract a shared
  `material/material_params.hpp` first.

## Round 1 build result

`xmake build -y vultra-app` → **build ok** after one fix: `FrameDebuggerSystem` needed an
out-of-line default constructor too (not just the destructor), else the implicit ctor
instantiated the `unique_ptr<RenderDocAPI>` deleter against the incomplete type in
`demo_app_host.cpp`. The other two RAII sites already had user-declared constructors.

## Round 2 (build-verified)

- **P1-3 validator**: added `validateToolArgs` + a `toolSchemas()` cache (built from
  `runtime_mcp::toolsList()`) in `runtime_mcp_tools.cpp`; `handleMcpRequest` now fails
  fast with an actionable message (missing required field / wrong type) before enqueue.
- **P2-11 step 1**: extracted `vultra/function/material/material_params.hpp` and replaced
  the **3 byte-identical** `MaterialParamsPBRMR` copies (render_system, asset_system,
  direct_gbuffer_pass). `compatibility_basecolor_pass.cpp` keeps its own *different*
  (smaller) layout — a distinct GPU ABI — and was intentionally left untouched.
- Both verified with `xmake build -y vultra-app` → build ok.

## Round 3 (build + test verified)

- **P2-13 / asset split step 2**: extracted vertex packing (`buildVertexAttributes`,
  `PackedVertexLayout`, `packVertices`) from `asset_system.cpp`'s anon namespace into a
  public `vultra/function/asset/mesh_vertex_packing.{hpp,cpp}` (types `vasset::VMesh` /
  `vasset::VVertexFlags` / `rhi::VertexAttributes`; `getSize` via ADL). `uploadMesh` call
  sites now resolve the `vultra::` names via the new header. Left the f16/quaternion
  helpers in `asset_system.cpp` (used by splat upload) to keep scope tight.
- Added `tests/mesh_vertex_packing/` (wired into `tests/xmake.lua`) asserting exact
  stride/offsets and packed bytes for position+normal and position+texcoord layouts.
- Verified: `xmake build -y test-mesh-vertex-packing` → build ok; `xmake run` →
  `mesh_vertex_packing: all checks passed`; `xmake build -y vultra-app` → build ok.

## Round 4 (build verified)

- **Asset split**: extracted the pure `vasset` memory-estimate helpers
  (`estimateV*Bytes`, `stringBytes`, `vectorBytes`) from `asset_system.cpp`'s anon
  namespace into `vultra/function/asset/asset_memory_estimate.{hpp,cpp}` (public
  `estimateVMesh/Texture/GaussianSplat/Skeleton/AnimationBytes`; the rest stay TU-local
  in the new cpp). `memoryStats()` resolves the `vultra::` names via the new header.
- Verified: `xmake build -y vultra-app` → build ok.

## GitHub issue audit (2026-06-03)

Two background sub-agents verified open issues against source; results applied via `gh`:

- **#7 Next-Generation Rendering**: checked **Hi-Z generation** (`hzb_generate_pass`) and
  **Hi-Z occlusion culling** (`meshlet_hiz_cull_pass`), which also completed parents
  *Core GPU-driven pipeline* and *Visibility & Depth*. Added an audit comment noting
  partial groundwork (GPU-driven shadows; visibility-buffer/GPU-culling debug via the
  frame debugger) and the still-missing items (tile lighting, Forward+, all GI, hybrid
  RT shadows/reflections, OMM, denoising, debug visualisations).
- **#8 Next-Gen World & Asset Management**: posted a status comment — Phases 1–5 are
  effectively **complete** (AssetHandle/AssetCache/AssetState, async vtask CPU load,
  deferred GPU upload, refcount GC, double-buffered RenderWorld + `RenderWorldCooker`,
  legacy MeshManager/TextureManager/`gfx::Mesh::create` all removed). Only outstanding
  item: making world cooking itself incremental/parallel via vtask. Suggested narrowing
  or closing the issue with a focused follow-up.

## Next steps

- **#8 follow-up**: parallelize `RenderWorldCooker::cook` via vtask (the one remaining
  gap), then the issue can close.
- **P2-11 remaining splits**: continue per `doc/architecture/render-system.md` and
  `asset-system.md`, building after each move (next candidates: asset builtin-IO,
  GPU upload, registry/import; render-system shader/graph/world cooking units).
- **P2-13 more tests**: minimal RHI/scene CPU tests as those surfaces get testable APIs.
- **P0-9 follow-on** (optional): auto-archiver for completed workspace logs older than
  N days.

## Blockers

- Local build verification depends on the `vshadersystem` xmake package; resolved with
  `xmake repo -u`. The user owns running the build in this session.
