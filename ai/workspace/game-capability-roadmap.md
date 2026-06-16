# Game-capability roadmap (next steps)

Created 2026-06-15. Purpose: a prioritized, self-contained backlog of engine features
that broaden the range of games VultraEngine can build. Each card has enough context
(goal, current state with file evidence, scope, acceptance) for a fresh session to spin
it into an `ai/specs/<name>.md` + `ai/tasks/<name>.md` and start work without re-auditing.

How to use: pick the highest-priority unblocked card, write a spec + task per the
`ai/specs/README.md` / `ai/tasks/README.md` conventions, implement, and check it off
here. Order within a priority band is the recommended sequence.

## Capability snapshot (as audited 2026-06-15)

Strong today: deferred PBR rendering (shadows/SSAO/SSR/bloom/FXAA/IBL/reflection probes/
4 light types/meshlets/RT/Gaussian-splat), single-player 3D gameplay skeleton (Jolt rigid
bodies + character controller, raycasts/overlaps, collision layers, sensor triggers +
enter/exit events), skeletal animation + animator state machine (cross-fade), 3D spatial
audio, ECS + reflected components, Lua scripting + hot reload + coroutines + event bus,
prefabs (with field overrides), scene load/switch (sync/async/additive), GPU particles,
and OpenXR (the standout — incl. the only existing action-input abstraction).

Hard gaps (block whole genres): gamepad/touch input, navigation/pathfinding, physics
joints/constraints, multiplayer networking, 2D toolkit (sprite/tilemap/2D physics).
Polish gaps: animation events / blend trees / root motion, save system, tween/timer
utilities, decals/fog, particle depth.

---

## P0 — remove hard blockers (highest ROI)

> **Status (2026-06-15): all of P0 implemented.** Specs/tasks under `ai/specs/` +
> `ai/tasks/`: `input-gamepad-action-map`, `navigation-recast`, `physics-joints`. Code,
> Lua bindings (regenerated), LuaLS stub, EN+CN docs, and demos landed. Final compile/run +
> `test-lua-api-conformance` verification is pending a local build (user runs builds; the
> first build fetches the `recastnavigation` package — run `xmake repo -u` if the index is
> stale). Per-card follow-ons are noted in each spec's "Out of scope".

### P0.1 Gamepad/controller input + desktop action mapping ✅ (done — Godot-style project config)
- **Goal:** first-class gamepad support and a key/mouse/gamepad/XR action-mapping layer
  (bindings + rebinding), so games aren't keyboard/mouse-only.
- **Unlocks:** controller-first / console / Steam Deck targeting, couch input, better
  action-game feel, player rebinding.
- **Current state:** ABSENT. Input is raw key/mouse polling only —
  `source/vultra/include/vultra/core/input/input_system.hpp`
  (`isKeyHeld/isMouseButtonHeld/...`), `core/input/input_structs.hpp`
  (`KeyCode`/`MouseCode`). No gamepad/joystick/touch anywhere. The ONLY action
  abstraction is XR-only: `source/vultra/include/vultra/function/openxr/xr_input.hpp`
  (`XRInputType`, `getBool/getFloat/getVector2/getPose`, interaction profiles).
- **Scope:** SDL3 already provides `SDL_Gamepad` (axes/buttons/rumble) and the engine
  already uses SDL3 — wire its gamepad events into the input system; add gamepad polling
  to `IInputService`; add a config-driven action map (action -> [key|mouse|pad|axis])
  that unifies with the XR action model; expose to Lua (`Input.getAxis`, `Input.getAction`,
  `Input.isActionPressed`, rumble). Update the Lua binding + LuaLS stub + conformance.
- **Acceptance:** a Lua script reads stick/trigger/button + a rebindable action; rumble
  works; `xmake run test-lua-api-conformance` PASS; doc in `doc/lua_scripting.md` (EN+CN).
- **Effort:** low–medium. **Depends on:** nothing.

### P0.2 Navigation / pathfinding (Recast/Detour + NavAgent) ✅ (done)
- **Goal:** bake a navmesh from level geometry and let agents path + avoid obstacles.
- **Unlocks:** AI movement, RTS, stealth/patrols, open-world NPCs, click-to-move RPG,
  enemy obstacle avoidance — the #1 gameplay-AI blocker (zero base today).
- **Current state:** ABSENT (verified earlier: no navmesh/recast/astar/pathfind in source).
- **Scope:** the `recastnavigation` package is in the project's xmake-repo (low-cost
  integration). Add a build-time/editor navmesh bake from mesh colliders / tagged geometry;
  a `NavMesh` asset; a `NavAgentComponent` (radius/height/speed/path) + a nav system that
  steers agents (or feeds the character controller); Lua API (`Nav.findPath`,
  `agent:setDestination`, etc.); editor visualization (debug-draw the navmesh + paths).
- **Acceptance:** an agent paths around an obstacle to a target in a demo scene; Lua can
  request a path; navmesh visualizes in the editor; conformance PASS; doc added.
- **Effort:** medium. **Depends on:** nothing (can reuse character controller for movement).

### P0.3 Physics joints / constraints (expose existing Jolt capability) ✅ (done)
- **Goal:** rigid-body constraints: hinge, fixed, distance, slider/prismatic, point,
  cone/swing-twist; plus a vehicle constraint.
- **Unlocks:** ragdolls, doors/levers/hatches, vehicles, chains/swings, articulated rigs.
- **Current state:** ABSENT in the engine, but Jolt supports it natively — the physics
  system already wraps Jolt: `source/vultra/src/function/physics/physics_system.cpp`,
  components under `source/vultra/include/vultra/function/world/components/*_shape_component.hpp`,
  `rigid_body_component.hpp`, `character_controller_component.hpp`. No
  `Jolt/Physics/Constraints/*` headers are included yet.
- **Scope:** add constraint component(s) referencing two bodies + params; create/destroy
  Jolt constraints in the physics system; Lua API to create/break/drive constraints (motor
  targets); optionally a `VehicleConstraint` wrapper (wheels/suspension/steering) as a
  follow-on. Editor inspector + gizmo for anchors is a nice-to-have.
- **Acceptance:** a hinge door and a distance/rope constraint work in a demo; a simple
  ragdoll (capsules + cone constraints) falls believably; Lua can create/break a joint;
  conformance PASS; doc added.
- **Effort:** medium (vehicle: +medium). **Depends on:** nothing.

---

## P1 — polish that broadens viable genres

> **Status (2026-06-16): all of P1 implemented.** P1.1 events + blend trees + root motion;
> P1.2 save (KV + slots) + DontDestroyOnLoad; P1.3 tween/timer/timeline. Specs/tasks:
> `animation-events`, `animation-blend-trees-root-motion`, `save-persistence`,
> `gameplay-tween-timer-timeline`. All shipped work has Lua bindings, LuaLS stub, EN+CN docs,
> and builds + passes conformance. Per-card follow-ons are noted in each spec's "Out of scope"
> (2D blend trees, single-clip root motion, nested-table KV values, etc.).

### P1.1 Animation: events ✅, blend trees ✅, root motion ✅
- **Goal:** keyframe **events** (fire SFX/damage/footstep callbacks), 1D/2D **blend
  trees** (directional locomotion), and **root motion** (animation-driven movement).
- **Unlocks:** polished action/RPG/platformer locomotion + combat.
- **Current state:** state machine + cross-fade PRESENT
  (`source/vultra/include/vultra/function/animation/animator_graph.hpp` States/Transitions/
  Conditions/Parameters; `animation_system.cpp:651` `BlendingJob`). ABSENT: events, blend
  trees (binary 2-state only), root motion, IK, layers/masks.
- **Scope (do events first — small, high-leverage):** add events to the animator graph +
  a dispatch into Lua (`on_anim_event(name)`); then blend-tree nodes feeding ozz
  `BlendingJob` with N layers; then root-motion extraction feeding transform/character
  controller. IK/layers/masks are explicitly out of scope for v1.
- **Acceptance:** a Lua callback fires on a footstep event; a 1D locomotion blend tree
  blends idle/walk/run by a speed param; (stretch) root motion drives a clip; conformance
  PASS; doc added.
- **Effort:** events low; blend trees medium; root motion medium. **Depends on:** nothing.

### P1.2 Save / persistence system ✅ (KV + slots + DontDestroyOnLoad)
- **Goal:** save slots, a key-value store, and a runtime-state serialization API exposed
  to Lua; plus a DontDestroyOnLoad-style persistence across scene loads.
- **Unlocks:** any progression game (RPG, metroidvania, roguelite).
- **Current state:** PARTIAL — scene serialization exists
  (`source/vultra/include/vultra/function/services/scene_service.hpp`
  `saveWorldAsSceneSync/captureWorldAsScene`; Lua `Scene.saveWorld/saveEntity`), but no
  save slots, no KV store, no selective game-state API, no cross-scene entity persistence.
- **Scope:** a `SaveService` with named slots (write/read/list/delete) backed by a file
  under the user data dir; a typed KV store usable from Lua (`Save.set/get/save/load`);
  a way to mark entities/worlds persistent across `Scene.instantiate(clearWorld=true)`.
- **Acceptance:** Lua writes player progress to slot 1, reloads the scene, reads it back;
  conformance PASS; doc added.
- **Effort:** medium. **Depends on:** nothing (scene serialization is the foundation).

### P1.3 Gameplay utilities: tween/easing + timers + simple timeline ✅ (done)
- **Goal:** tweening/easing, delayed/scheduled callbacks, and a minimal sequencer.
- **Unlocks:** game-feel/juice, UI animation, cutscene-ish sequences across all genres.
- **Current state:** ABSENT except Lua coroutines (`script_coroutine_binding.cpp`,
  `wait()/waitFrames()`) and the `EventCenter` bus (`core/event/event_center.hpp`).
- **Scope:** mostly a Lua-side library (can live in builtin scripts) — `Tween.to(target,
  fields, duration, ease)`, easing functions, `Timer.after(s, fn)` / `Timer.every(s, fn)`,
  and a tiny `Timeline` that schedules ordered actions. Hook into the per-frame update.
- **Acceptance:** a value tweens with an easing curve; a delayed callback fires once; an
  interval callback repeats; doc added. (May not touch C++/conformance at all.)
- **Effort:** low. **Depends on:** nothing.

---

## P2 — feel / VFX / engine identity

### P2.1 Particle/VFX depth (toward a Niagara-like)
- **Goal:** particle collision, sprite-sheet/flipbook, trails/ribbons, sub-emitters,
  per-emitter custom material; later a modular VFX graph.
- **Current state:** PARTIAL — GPU compute billboards only
  (`source/vultra/include/vultra/function/particle/gpu_particle_manager.hpp`,
  `particle_emitter_component.hpp`; passes `particle_simulate_pass`/`particle_render_pass`).
  No collision/flipbook/trails/mesh/sub-emitters/custom-material. `worldSpace` field is
  currently a no-op (see `doc/particle_system.md`).
- **Scope (incremental):** flipbook + per-emitter material first; then depth-buffer
  collision in the compute pass; then trails and sub-emitters. A VFX graph is a later epic.
- **Acceptance:** a flipbook emitter and a depth-colliding emitter render in a demo; doc updated.
- **Effort:** medium–high. **Depends on:** nothing.

### P2.2 Rendering: decals + fog
- **Goal:** deferred decals (bullet holes/blood/footprints) and height/volumetric fog.
- **Current state:** ABSENT (no decal/fog passes or components). Renderer is a declarative
  render graph (`builtin/render/universal.vrg.json` + `declarative_renderer.cpp`), so both
  fit as new builtin passes per the project's "render feature as builtin pass" convention.
- **Scope:** a `DecalComponent` + a deferred decal pass projecting onto the GBuffer; a fog
  pass (exponential height fog first, volumetric later) wired into the universal graph.
- **Acceptance:** a decal projects onto geometry; height fog renders; both editable in the
  render graph; doc added.
- **Effort:** medium. **Depends on:** nothing.

### P2.3 Visual scripting (Blueprint → generated Lua)
- **Goal:** a node-graph gameplay scripting surface that compiles to Lua.
- **Unlocks:** designer-friendly authoring; aligns with the engine's graph tooling.
- **Current state:** ABSENT for gameplay (render/material/animator graphs exist). NOTE:
  ImNodes is now bound to Lua (`source/vultra/src/function/scripting/bindings/
  script_imgui_ext_binding.cpp`), so the node-editor UI is available from Lua already.
- **Scope:** a node schema + a graph→Lua code generator (reuse the codegen mindset), an
  editor window (imnodes), and a `.vbp`/`.vblueprint` asset. Ranked below core systems
  because Lua already covers scripting needs.
- **Effort:** medium. **Depends on:** nothing (but lower urgency).

---

## P3 — large / strategic

### P3.1 Networking / multiplayer
- **Goal:** transport + replication + RPC for gameplay (optionally rollback).
- **Current state:** ABSENT (MCP/RPC is editor tooling, not gameplay net).
- **Scope:** big epic — pick a transport, define replicated components, snapshot/interp or
  rollback. Defer unless multiplayer is an explicit product goal.
- **Effort:** high.

### P3.2 2D toolkit
- **Goal:** sprite + atlas + tilemap + 2D sort layers (optionally Box2D for 2D physics).
- **Current state:** ABSENT (only UI Image + orthographic camera). The engine is 3D-first.
- **Scope:** strategic pivot; only if 2D is a target. A `SpriteComponent` + tilemap +
  sorting can layer on the existing renderer; 2D physics would add Box2D alongside Jolt.
- **Effort:** medium–high.

### P3.3 GI / lightmaps / LOD / occlusion data
- **Goal:** baked lightmaps or real-time GI, traditional mesh LOD chains, precomputed
  occlusion.
- **Current state:** ABSENT (GPU-driven HZB culling + meshlet/Gaussian-CLOD exist, so
  culling is partly covered; no GI/lightmaps/LOD-chains).
- **Effort:** medium–high; lower urgency given current culling coverage.

---

## Recommended path

For "ship more single-player 3D genres fastest": **P0.1 gamepad → P0.2 navigation →
P0.3 joints → P1.1 animation events → P1.2 save**. After those five, the engine covers
most mainstream single-player 3D genres. The VR/research direction is already the
strongest area; P2.1 (VFX) and P3.3 (GI) give it the most additional lift.

Suggested starting card for a fresh window: **P0.1 (gamepad input)** or **P0.2 (Recast
navigation)** — both are self-contained, unblock whole genres, and reuse existing systems.
