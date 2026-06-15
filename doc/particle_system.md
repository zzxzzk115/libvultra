# Particle system

**English** | [简体中文](zh_CN/particle_system_CN.md)

A particle system driven by a `ParticleEmitterComponent`, with two interchangeable backends.

> **Status:** the default backend simulates particles in a **GPU compute shader** and renders them as
> instanced, camera-facing **additive billboards** (soft, depth-faded) via builtin render-graph
> passes. A **CPU** backend (debug-draw preview) remains as a fallback. The `gpu` field on the
> component selects the backend (default `true` = GPU).

## Component

[`ParticleEmitterComponent`](../source/vultra/include/vultra/function/world/components/particle_emitter_component.hpp)
is attached to an entity that also has a `TransformComponent`; particles spawn from the entity's
world position.

| Field | Meaning |
|-------|---------|
| `playing` | emit or pause |
| `worldSpace` | authoring-only no-op (see Roadmap): simulation origin is always the emitter's world matrix; local-space simulation is unimplemented |
| `gpu` | backend select: GPU compute + billboards (`true`, default) or CPU debug-draw (`false`) |
| `maxParticles` | hard cap on live particles (GPU: fixed pool size) |
| `emissionRate` | particles spawned per second |
| `lifetime` / `lifetimeVariance` | seconds, ± fraction |
| `spawnRadius` | random spawn offset around the emitter |
| `startVelocity` / `velocityVariance` | initial velocity + random magnitude |
| `gravity` | constant acceleration |
| `startSize` / `endSize` | size interpolated over life |
| `startColor` / `endColor` | colour (RGBA) interpolated over life |

The component is registered for reflection, scene (de)serialization, the scene component registry,
the editor inspector, and the add-component menu, so it round-trips in `.vscn`/`.vmanifest` and is
fully editable in the editor.

## GPU backend (default)

When `gpu == true` the emitter is driven entirely on the GPU:

1. **Gather + manage** — `RenderWorldCooker::cook` collects GPU emitters into `RenderWorld::emitters`.
   [`GpuParticleManager`](../source/vultra/include/vultra/function/particle/gpu_particle_manager.hpp)
   (owned by `RenderSystem`) keeps a persistent particle SSBO per emitter (a fixed pool of
   `maxParticles` [`GpuParticle`](../source/vultra/include/vultra/function/particle/gpu_particle.hpp)),
   advances a CPU emission accumulator + round-robin spawn cursor each frame, and publishes a
   per-emitter draw list onto the active `GpuSceneView`.
2. **Simulate** — `ParticleSimulatePass` (compute) respawns the round-robin spawn window and
   integrates the rest (`velocity += gravity·dt; position += velocity·dt`), ageing particles out.
   Dead slots collapse to a degenerate billboard.
3. **Render** — `ParticleRenderPass` (graphics) draws one instanced, camera-facing billboard per
   pool slot with additive blending and a soft circular sprite + soft-depth fade against the scene
   depth, in HDR before tone mapping.

These are not two separate graph nodes: a single builtin `ParticleRender` graph node — wired into
`universal.vrg.json` (and the project `default.vrg.json`) between `GeneralGaussianSplatComposite`
and `Ssr` — runs both passes internally. Its handler calls `ParticleSimulatePass` (compute) and
then `ParticleRenderPass` (graphics) in sequence. The builtin graph also ships tier variants
`universal_compat.vrg.json` and `universal_rt.vrg.json` (the default file is still named
`universal.vrg.json`).

## CPU backend (fallback)

When `gpu == false`,
[`ParticleSystem`](../source/vultra/include/vultra/function/particle/particle_system.hpp) — an engine
subsystem emplaced **before** `RenderSystem` — simulates on the CPU and previews each live particle
through the debug-draw path. It skips emitters whose `gpu == true`. It is wired into the standard
subsystem set in [demo_app_host.cpp](../source/vultra/src/core/app/demo_app_host.cpp).

## Editor

- **Add Component → Rendering → Particle Emitter** adds the component; all fields are editable in
  the inspector (reflection-driven).
- The scene view shows a ✦ (`ICON_MDI_CREATION`) gizmo icon at each emitter, click-selectable.
- The sample scene [resources/scenes/test.vmanifest](../resources/scenes/test.vmanifest) has a
  "Sparks" emitter for reference.

## Implementation notes

- The GPU pool is a **fixed-size, round-robin** ring (`maxParticles` slots). The CPU advances an
  emission accumulator and a spawn cursor; the compute shader respawns the `[cursor, cursor+emitCount)`
  window and integrates the rest. A freshly created/resized pool sets a reset bit so the shader seeds
  it without a host clear. There is no GPU dead/alive list, indirect dispatch, or back-to-front sort
  yet — overlapping translucent particles use additive blending (order-independent).
- Pools are keyed per emitter entity and persist across frames; emission is advanced at most once per
  frame even when an emitter is rendered by multiple views (e.g. the editor scene + game views).

## Roadmap

- Texture/atlas sprites, emission shapes, color/size-over-life curves, and sub-emitters.
- Alive/dead-list recycling + indirect dispatch/draw and optional depth sorting (the RHI already has
  a radix sorter) for large alpha-blended systems.
- Local-space simulation: the `worldSpace` field is currently reflected/serialized/scriptable but
  read by neither backend (the emitter's world matrix is always the origin), so it is an
  authoring-only no-op until local-space integration lands.
