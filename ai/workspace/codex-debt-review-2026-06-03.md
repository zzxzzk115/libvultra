# Codex-Debt Code Review — Engineering Audit

Date: 2026-06-03
Status: Done (written audit only — no code changed this round)
Scope: AI engineering layer (harness / Runtime MCP / cleanup) **and** C++ core engineering, weighted equally.

## Summary

A full critical audit of libvultra (VultraEngine). The architectural skeleton is sound
— the layering (core / function / platform), the RHI interface abstraction, the
scriptable render pipeline, and the data-driven asset model are all mature. **The
execution, however, is dirty.** Across both the AI tooling and the C++ core, the
recurring failure is the absence of *convergence mechanisms*: no de-duplication, no
lifecycle management, no cleanup, no indexing, no validation. Every "patch-style"
change therefore raises entropy, which is exactly why an AI agent (and a human) finds
the project progressively harder to read, use, and modify safely.

Findings are tagged **[verified]** (read directly from source during this review) or
**[needs recheck]** (relayed by exploration sub-agents; confirm against source before
acting). Two earlier sub-agent claims were **wrong and have been corrected** below, to
avoid acting on bad conclusions.

### Honesty corrections (do not act on these — they were false alarms)
- The MCP result wrapper `{"content":[{"type":"text"}]}`, the error wrapper
  `{"isError":true}`, and `protocolVersion:"2025-03-26"` were claimed "non-standard".
  **They are in fact exactly what the MCP spec requires.** Not a defect.
- A timed-out `PendingCall` was claimed to be a "permanent memory leak re-executed
  forever". **Not true** — once `defer` resolves, the call sets `done=true` and is
  released. The real defect is different (see P0-2).

---

## 1. Runtime MCP server — the clearest specimen of patch-style code

### P0-1 — Tool dispatch is a giant copy-pasted if-else chain **[verified]**
`source/vultra_app/src/editor_app/runtime_mcp_tools.cpp:194-332` contains ~15
`vultra.scene.*` branches that are the *same 9-line template* pasted verbatim,
differing only by the tool/command name string:

```cpp
if (name == "vultra.scene.XXX") {
    if (!ctx.editor) return toolError("editor command executor is unavailable");
    auto result = ctx.editor->executeCommand(ctx, "scene.XXX", args);
    if (!result.value("ok", false)) return toolError(result.value("error", "scene.XXX failed"));
    return toolJson(std::move(result));
}
```

This is textbook patch-style growth: every new tool = another pasted block.
Consequences: a reader scans 343 lines only to discover "it's really just a lookup
table"; a change must be mirrored in 15 places; the tool-name → command-name mapping
has no single source of truth.

**Remediation:** replace all 15+ branches with one `{toolName → editorCommandName}`
table plus a single `dispatchEditorCommand()`. Keep `vultra.editor.command`,
`command_batch`, and `window` as explicit special cases. ~140 lines collapse to
~30 lines + a table.

### P0-2 — Deferred calls have no deadline → orphaned mutations **[verified]**
`runtime_mcp_tools.cpp:84-87`: the HTTP thread waits `kToolTimeout = 5s`
(`runtime_mcp_tools.cpp:20`) and returns an error on timeout, then stops waiting.
But `runtime_mcp_server.cpp:387-435` (`executePending()`) keeps re-running any
`defer=true` call *every frame* until its internal condition resolves.
`PendingCall::frameCaptureStartedAt`
(`source/vultra_app/include/editor_app/runtime_mcp_server_internal.hpp:30`) is recorded
but **never used as a timeout**. The real defects (not a leak):
- after the client gives up at 5s, the main thread is *still running* the call and may
  mutate the scene/assets after the caller has left → **orphaned state mutation** with
  unpredictable results;
- if a deferred condition never resolves (e.g. a stuck frame capture), the call spins
  every frame and **never converges**.

**Remediation:** give `PendingCall` an absolute deadline (use `frameCaptureStartedAt`);
in `executePending`, fail+dequeue expired deferred calls and stop touching `ctx`.
The HTTP timeout and the main-thread deadline should share one source.

### P1-3 — Tool implementations never validate against the published `inputSchema` **[verified / partly needs recheck]**
Every parameter is read loosely via `args.value(...)` (e.g.
`runtime_mcp_tools.cpp:105-109`); the schema published by `tools/list` has **no
enforced correspondence** to execution. When an agent passes a wrong type or omits a
field it either silently takes a default or gets a generic "XXX failed", with no way to
self-correct.
- **[needs recheck]** inconsistent parameter naming inside schemas (`component_kind` vs
  `componentKind` vs `kind`) and an empty `{}` schema for `entity`, in
  `runtime_mcp_tool_registry.cpp`.

**Remediation:** do a minimal pre-dispatch validation (required fields / types) against
the published schema and return structured, actionable error messages; standardize on
one casing (camelCase) and document it in knowledge.

### P1-4 — Coarse resource lifecycle **[needs recheck]**
Relayed by sub-agents, confirm against source: stream client threads have no upper
bound (`runtime_mcp_server.cpp:637-697`); `closePipe()` (ffmpeg) has no timeout, so a
hung process blocks shutdown; when the video queue is full, all stale frames are
dropped silently and only counted. These are typical "make it run first, never finish
the teardown" patches.

---

## 2. AI Harness flow — a playbook, not a framework

### P0-5 — `ai/workspace/` has degenerated into 70 un-indexed, lifecycle-less logs **[verified]**
Counts: `workspace 70` files vs `knowledge 7 / specs 10 / tasks 15 / agents 1`.
The 70 handoff/verification logs are inconsistently formatted (some have `Date:`, most
do not), have no `Status (Done/In-Progress/Blocked)`, no index, and no archival.
**This is the number-one reason an agent "can't read" the repo**: a fresh agent landing
here must guess which of 70 similar files is still relevant.

**Remediation:**
- one template: `Date / Status / Summary / Result / Next steps / Blockers`
  (this very file follows it);
- add `ai/workspace/README.md` as an index and move completed logs to
  `workspace/archive/`;
- longer term: a script that auto-archives completed logs older than N days.

### P1-6 — Skill / agent metadata is decorative and not machine-usable **[verified]**
Each `ai/skills/*/agents/openai.yaml` is only 4 lines (display_name /
short_description / default_prompt), with no `prerequisites / in_scope / out_of_scope /
linked task / linked knowledge`. `ai/agents/README.md` lists 5 role names with no
selection criteria and no interaction model. Result: an agent cannot auto-select a
skill or judge its boundaries — it must infer everything from prose.

**Remediation:** enrich the metadata into a machine-readable form (required reading,
scope boundaries, task↔skill mapping, verification steps) and add an `@skill xxx`
back-reference inside task files.

### P1-7 — Entry docs lack a decision tree and a "when is MCP available" rule **[verified]**
`AGENTS.md` / `ai/README.md` describe the 8-step flow clearly but omit:
- a decision tree for a fresh agent ("Is MCP up? How to degrade if not? What if a task
  is blocked?");
- task prioritization (15 tasks with no priority labels — an agent doesn't know where
  to start);
- a canonical smoke-command list (the "smallest command set" phrasing is too vague);
- a single `ai/harness-config` (MCP host/port and build target are currently hardcoded
  to 8848 across docs and the Python client).

### P1-8 — Knowledge files contain hardcoding and mix "now" with "aspiration" **[verified / needs recheck]**
- `ai/knowledge/vscode-workspace.md` hardcodes the Windows path
  `C:\Program Files\LLVM\bin\clang-format.exe` — breaks the moment it is copied to
  another environment;
- `ai/knowledge/ai-runtime-rpc.md` interleaves long-term vision (shared memory / GPU
  interop) with what is implemented today — an agent cannot tell what actually works;
- `vultra-formats.md` documents old vs new field names (`typeId/nodeId` vs `type/node`)
  but with no deprecation markers / migration notes and no mechanism that forces
  updates → it will drift stale.

---

## 3. "Not cleaned up" — temp artifacts and build entropy

### P1-9 — MCP smoke tests leave 18 timestamped dirs in `build/.tmp/` that are never reclaimed **[verified]**
`build/.tmp/` was observed to contain ~18 directories such as
`material-mcp-smoke-20260603-142629 … 144321`, `material-mcp-smoke-output-*`,
`material-mcp-step-*`, `material-shader-frame-textures`, holding generated
`.vmat.json`, PNGs and build output.
**Clarification:** `build/` is already `.gitignore`d (`build/`, `**/build/`), so these
are **not** committed — the sub-agent's "could be committed" worry is unfounded. But the
problem is real: the smoke-test workflow **creates a new timestamped dir on every run
and never reclaims it**, so local disk grows unbounded with no cleanup script or
convention. This is the concrete form of "not cleaned up".

**Remediation:** have smoke tests write to a single reusable dir or clean up on exit;
provide a `tools/clean-mcp-tmp` script; document the temp location and cleanup
convention in the harness docs.

---

## 4. C++ core engineering — patches + god files + assert-as-error-handling

### P1-10 — Raw new/delete contradicts the library's RAII style **[needs recheck — samples credible]**
The codebase leans heavily on `unique_ptr`/`shared_ptr` yet scatters manual
`new/delete`, which leak on exception paths:
- `source/vultra/src/function/debugging/frame_debugger_system.cpp:13`
  `new RenderDocAPI(...)` + `delete` in the destructor;
- `source/vultra/src/function/physics/physics_system.cpp:302` `new Job(...)` /
  `delete job`;
- `source/vultra/src/function/openxr/xr_common_action.cpp:17` `new XREyeTracker`;
- `source/vultra/src/function/rendering/srp/builtin/features/depth_hzb_feature.cpp:11`
  `new DepthPrePass()/new HzbGeneratePass()`.

**Remediation:** convert to `unique_ptr`/`make_unique`; `.clang-tidy` is already
`WarningsAsErrors:*`, so add a `cppcoreguidelines-owning-memory`-style check to prevent
regressions.

### P2-11 — God files **[needs recheck]**
`source/vultra/src/function/rendering/render_system.cpp` (~4600 lines) and
`source/vultra/src/function/asset/asset_system.cpp` (~1700 lines) mix many
responsibilities in one TU with sparse comments. An AI cannot load the whole file to
reason about it, and edits easily regress. **Remediation:** split by responsibility
(e.g. render_system → setup / passes / submit / debug).

### P2-12 — assert used as error handling **[needs recheck]**
e.g. `source/vultra/src/core/rhi/buffer.cpp:24-63` uses `assert(m_Impl)` everywhere;
these vanish in Release builds → silent failure. **Remediation:** for
external/recoverable errors use explicit return values or exceptions; keep `assert` only
for unreachable invariants.

### P2-13 — Thin docs and tests **[needs recheck]**
100+ modules but only 2 markdown docs (`gpu_driven_pipeline.md`, `lua_scripting.md`);
headers carry almost no docstrings; `tests/` has only ~5 hand-written mains and no test
runner. **Remediation:** add architecture docs for key subsystems and at least minimal
automated tests for RHI / asset / scene.

---

## 5. Prioritized remediation roadmap

> This round delivers the written report only. Ordering below is for later, phased work.

| Tier | Item | Files | Value |
|---|---|---|---|
| **P0** | 1. De-duplicate MCP dispatch into a table | runtime_mcp_tools.cpp | Kills the most typical patch-style code |
| **P0** | 2. Deferred-call deadline + orphaned-mutation guard | runtime_mcp_server.cpp / _internal.hpp | Fixes a real correctness defect |
| **P0** | 9. Temp-artifact cleanup script + convention | tools/ + harness docs | Directly resolves "not cleaned up" |
| **P1** | 5. workspace template + index + archive | ai/workspace/ | Fixes the #1 "AI can't read it" cause |
| **P1** | 3/6/7. MCP input validation, machine-readable skill metadata, harness decision tree + config | ai/ + tool_registry | Lets an agent work self-sufficiently |
| **P1** | 8. Remove hardcoding, separate now/vision, format deprecation markers | ai/knowledge/ | Prevents agent misuse and drift |
| **P1** | 10. Raw new/delete → RAII + tidy guard | the four sites above | Engineering baseline |
| **P2** | 11/12/13. Split god files, fix assert usage, add docs and tests | core engine | Long-term maintainability |

---

## Verification (for when work is actually done)
- **MCP refactor:** `xmake build -y vultra-app` →
  `xmake run vultra-app --editor --mcp --project example.vproject --no-xr` →
  run `initialize` / `tools/list` / `tools/call vultra.runtime.status` plus a sample of
  5 `scene.*` tools, confirm the dispatch table matches the old if-else behavior;
  construct a timeout scenario and confirm deferred calls no longer mutate the scene.
- **Cleanup script:** run a smoke test once and confirm `build/.tmp/` no longer
  accumulates timestamped dirs.
- **Harness hygiene:** have a brand-new agent bootstrap from `AGENTS.md` alone and
  locate the current task *without* reading 70 workspace files.
- **C++ RAII:** `xmake build -y vultra` is warning-free with the new tidy check enabled.

## Items to recheck before touching code
P1-3 schema naming inconsistency / P1-4 thread & ffmpeg teardown / P1-10 full raw-new
inventory / P2-11/12/13 line counts and assert distribution — all relayed by sub-agents;
read the source for each before acting, so the fix lands in the right place.
