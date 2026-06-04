# Particle system

A lightweight particle system driven by a `ParticleEmitterComponent`.

> **Status:** v1 simulates particles on the **CPU** and previews them through the **debug-draw**
> path (a small box per particle). The component data is backend-agnostic; a **GPU compute +
> instanced billboard** backend is the planned upgrade (see Roadmap).

## Component

[`ParticleEmitterComponent`](../source/vultra/include/vultra/function/world/components/particle_emitter_component.hpp)
is attached to an entity that also has a `TransformComponent`; particles spawn from the entity's
world position.

| Field | Meaning |
|-------|---------|
| `playing` | emit or pause |
| `worldSpace` | simulate in world space (vs local to the emitter) |
| `maxParticles` | hard cap on live particles |
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

## System

[`ParticleSystem`](../source/vultra/include/vultra/function/particle/particle_system.hpp) is an
engine subsystem emplaced **before** `RenderSystem` so the particles it spawns queue their
debug-draw preview for the same frame. Each frame it: GCs dead emitters, spawns at the emission
rate (capped), integrates `velocity += gravity·dt; position += velocity·dt`, ages particles out,
and previews each live particle via `IRenderService::debugDrawBox`.

It is wired into the standard subsystem set in
[demo_app_host.cpp](../source/vultra/src/core/app/demo_app_host.cpp), so every app built on
`DemoAppHost` (examples, the runtime player) gets it.

## Editor

- **Add Component → Rendering → Particle Emitter** adds the component; all fields are editable in
  the inspector (reflection-driven).
- The scene view shows a ✦ (`ICON_MDI_CREATION`) gizmo icon at each emitter, click-selectable.
- The sample scene [resources/scenes/test.vmanifest](../resources/scenes/test.vmanifest) has a
  "Sparks" emitter for reference.

## Roadmap (GPU backend)

The CPU path is intentionally a thin, correct foundation. A GPU upgrade would:

1. Store particles in a GPU buffer; spawn + integrate in a **compute pass** (the RHI already has
   compute pipelines and a radix sorter for back-to-front sorting).
2. Render as **instanced camera-facing billboards** with a soft particle / additive material,
   added as a builtin render-graph pass.
3. Keep `ParticleEmitterComponent` as the authoring front-end; add texture/atlas, emission shapes,
   curves, and sub-emitters.
