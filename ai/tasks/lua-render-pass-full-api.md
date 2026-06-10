# Fully-Open Lua Render Pass API

Date: 2026-06-10
Status: Planned (not started)

## Goal

Let a Lua scripted render pass do anything a builtin C++ render pass can — create
buffers/textures/samplers/pipelines with full state, record arbitrary draw / dispatch /
indirect / barriers, and read the scene (RenderWorld / camera / lights / GPU-scene
buffers). The API is **fully opened**; correctness/safety risk is borne by the pass
author, not enforced by the binding layer.

## Why

Today's Lua scripted passes are deliberately conservative — fullscreen post-process plus
simple compute only (see `resources/render/passes/invert.lua`, `pixelate.lua`). They
**cannot mirror a complex builtin pass** (DeferredLighting, ShadowMap, MeshletCull, …),
so any non-trivial render feature still requires C++. The goal is to remove that ceiling.

## Current surface (baseline)

- `LuaPassBuildContext` (~17 methods) + `LuaPassExecContext` (~8 methods), bound via sol2
  in `source/vultra/src/function/rendering/srp/declarative_renderer.cpp` (~lines
  2802-3292). `ScriptedPassPipelines` builds pipelines with **fixed** state (no
  depth/blend/cull, fullscreen-triangle topology only).
- Hard gaps vs builtin passes: no `rhi::RenderDevice`, no `rhi::CommandBuffer`, no buffer
  I/O (texture-only), no configurable pipeline state, no vertex-stage bindings, no
  geometry/indirect draws, no RenderWorld/camera/light traversal (only `sceneDrawCount`/
  `hasGaussianSplats`/`isGpuDriven` query flags), no ray tracing.

## In scope

A Lua pass holds the same handles a C++ builtin pass does: the `FrameGraphBuildContext`,
the exec context `rc` (`rc.cb`, `rc.rd`, `rc.framebufferInfo()`, `rc.view()`, `rc.ext`),
`rhi::RenderDevice&`, and `rhi::CommandBuffer&`.

## Out of scope (for now)

- Ray tracing pass authoring from Lua (raygen/miss/hit) — separate follow-up.
- Changing the builtin C++ pass set or the builtin-pass self-registration
  (`IBuiltinRenderGraphPass`) that just landed.

## Relevant links

- `ai/knowledge/lua-scripting.md` — Lua scripting conventions.
- `doc/scripted_render_passes.md` — current scripted-pass documentation (to be expanded).
- `ai/tasks/xr-default-view-synthesis.md` — related render-graph work.

## Implementation plan (phased, highest value first)

- **Phase 0 — research (do FIRST).** Map the sol2 binding surface needed: `rhi::RenderDevice`
  (create buffer/texture/sampler/graphics+compute pipeline), `rhi::CommandBuffer`
  (bind/draw/dispatch/indirect/barrier/push-constants), `rhi::GraphicsPipeline::Builder`
  full state, `FrameGraph::Builder` buffer resources (`FrameGraphBuffer`, `read`/`write`,
  `importStorageBuffer`), the exec `rc`, and `RenderView`/`RenderWorld`/`RenderCamera`/
  `RenderLight` (`render_structs.hpp`). Catalog the project's existing sol2 binding
  conventions (how gameplay binds C++ types; lifetime/RAII rules).
- **Phase 1 — Buffer I/O (biggest win, lowest cost).** `createBuffer`, `readBuffer`,
  `writeStorageBuffer` in setup; `getResource("InstanceBuffer"/…)` already exposes
  GPU-scene buffers. Unlocks real compute work (culling, prefix-sum, etc.).
- **Phase 2 — Configurable graphics pipeline state.** Depth test/write, blend, cull,
  topology, + vertex-stage descriptor bindings. Extend `ScriptedPassPipelines` and the
  `useGraphicsShader` spec table instead of the hardcoded fullscreen config.
- **Phase 3 — Raw escape hatch (core of "fully open").** Bind `rhi::RenderDevice&` and
  `rhi::CommandBuffer&` (and `FrameGraphBuildContext` / exec `rc`) directly as sol
  usertypes so Lua gets the same objects C++ has; everything else becomes reachable.
- **Phase 4 — Geometry / indirect draws.** GPU-scene indirect draw, bind vertex/index
  buffers, `drawIndexed`, `dispatchIndirect`.
- **Phase 5 — Scene access.** Bind `RenderView`/`RenderWorld`/`RenderCamera`/`RenderLight`
  (read-only) so Lua can iterate lights/camera and replicate lighting passes.

## Risk stance (accepted)

Raw `RenderDevice`/`CommandBuffer` access **bypasses the FrameGraph's automatic barriers
and descriptor management** — a Lua pass can corrupt the frame or crash the GPU/process.
Per decision this is acceptable. Existing mitigations stay: scripted `execute` runs on the
render-script thread (asserted) and `setup`/`execute` are sol `protected_function`s
(errors logged once, work skipped). Document the footguns loudly in
`doc/scripted_render_passes.md`.

## Critical files

- `source/vultra/src/function/rendering/srp/declarative_renderer.cpp` —
  `LuaPassBuildContext` / `LuaPassExecContext` / `ScriptedPassPipelines` + the sol2
  registration block.
- rhi headers to bind: `render_device.hpp`, the command-buffer header,
  `graphics_pipeline.hpp`, buffer/texture/sampler headers.
- `source/vultra/include/vultra/function/rendering/render_structs.hpp` —
  `RenderView`/`RenderWorld`/`RenderCamera`/`RenderLight` for scene access.
- `doc/scripted_render_passes.md` — document the expanded surface + risks.

## Verification plan

New example `.lua` passes exercising: (a) a buffer-backed compute pass, (b) a
custom-pipeline-state graphics pass (depth + blend), (c) an indirect geometry draw. Launch
the editor (`xmake build -y vultra-app`), confirm each renders, and confirm an intentional
misuse fails safe (logged, frame continues) rather than taking down the editor.

## Status / handoff

Not started. This task was split out from the builtin-pass self-registration refactor
(landed separately). Begin at Phase 0 research before writing any bindings.
