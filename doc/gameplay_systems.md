# Vultra Gameplay Systems

**English** | [简体中文](zh_CN/gameplay_systems_CN.md)

This guide consolidates the four runtime gameplay subsystems: **physics**,
**audio**, **animation**, and **in-game UI**. Each runs as an engine subsystem
and is driven by entity components.

- Scripting lifecycle, callbacks, and the full Lua surface live in
  [Lua scripting](lua_scripting.md).
- Component fields, serialization, and the editor inspector live in
  [Scene & components](scene_and_components.md).

All four systems honor playback state (`setPlaybackState`) so they only
simulate while editor/runtime playback is active.

---

## Physics

**Built on:** [Jolt Physics](https://github.com/jrouwe/JoltPhysics), wrapped by
`PhysicsSystem` (`source/vultra/include/vultra/function/physics/physics_system.hpp`).
The system manages Jolt globals, builds one Jolt body per eligible entity, and
steps the simulation on a **fixed timestep** (default `1/60 s`). It substeps to
catch up with elapsed time using the timing service's fixed-step count, with a
fallback accumulator capped at `m_FallbackMaxSubSteps` (8). Single-stepping is
supported while paused (`requestSingleStep`).

### Components

A physics body needs a `RigidBodyComponent` plus one collider shape on the same
entity:

- **`RigidBodyComponent`** — `motionType` (0 static / 1 kinematic / 2 dynamic),
  `objectLayer`, `isSensor`, `motionQuality` (0 discrete / 1 linear cast),
  `allowSleeping`, `friction`, `restitution`, `linearDamping`, `angularDamping`,
  `gravityFactor`, `linearVelocity`, `angularVelocity`, `mass` /
  `overrideMass`, `maxLinearVelocity`, `maxAngularVelocity`.
- Collider shapes (one per body):
  - **`BoxShapeComponent`** — `halfExtents` (vec3).
  - **`SphereShapeComponent`** — `radius`.
  - **`CapsuleShapeComponent`** — `halfHeightOfCylinder`, `radius`.
  - **`CylinderShapeComponent`** — `halfHeight`, `radius`.
  - **`MeshShapeComponent`** — collider baked from the entity's
    `MeshComponent`; `convex == false` is a static triangle mesh (level
    geometry, not for dynamic bodies), `convex == true` is a convex hull usable
    on dynamic bodies.

### Character controller

`CharacterControllerComponent` is a kinematic controller backed by Jolt's
`CharacterVirtual` (collide-and-slide, slope limit, stair stepping, ground
detection) — distinct from rigid bodies. The controller position is the
capsule's feet. Gameplay drives it via `inputMove` (desired horizontal
velocity) and `jumpRequested` / `jumpSpeed`; the system writes back `velocity`
and `grounded` each step. Tuning fields: `radius`, `height`,
`maxSlopeAngleDegrees`, `stepHeight`, `gravityFactor`, `mass`, `objectLayer`.

### Queries

`PhysicsSystem` exposes narrow-phase queries (real Jolt geometry, not AABB):
`raycast` / `raycastAll`, `sphereCast`, and `overlapSphere` / `overlapBox` /
`overlapCapsule`. All take a `PhysicsQueryFilter` (active-only + a 32-bit layer
mask over `objectLayer` indices).

### Collision and trigger callbacks

After each physics update the system diffs contact pairs and dispatches
**real Lua callbacks** to scripts on either entity:
`OnCollisionEnter/Stay/Exit` and `OnTriggerEnter/Stay/Exit`. Trigger callbacks
fire when either body has `isSensor = true`. On exit events the `other` entity
may already be invalid — guard with `other.valid`. Polling via
`Physics.contactEvents()` / `consumeContactEvents()` remains available.

### Important: drive movement with velocity, not force

For gameplay character/object movement, **set `rigidBody.linearVelocity`**
rather than calling `addForce` / `addImpulse`. This is verified in
`syncDynamicBodiesToWorld` (`source/vultra/src/function/physics/physics_system.cpp`):
every step, for dynamic bodies, the system **reads the Jolt body's velocity back
into the component** (`rb->linearVelocity = GetLinearVelocity(...)`). The
component value is only pushed to the body at creation and for kinematic bodies,
so a velocity you write per-frame is the authoritative one Jolt simulates with,
while accumulated forces/impulses from prior frames are effectively overwritten
by the read-back. `addForce` / `addImpulse` still exist for instantaneous
impulses but are not a reliable way to express sustained movement.

> Lua API: see the **Physics** section of [Lua scripting](lua_scripting.md) —
> `entity.rigidBody` (`linearVelocity`, `addForce`, `addImpulse`, ...) and the
> global `Physics` table (`raycast`, `overlapSphere`, layer collision, ...).

---

## Audio

**Built on:** [miniaudio](https://miniaud.io/), wrapped by `AudioSystem`
(`source/vultra/include/vultra/function/audio/audio_system.hpp`). Mixing uses
`ma_engine` (WASAPI/CoreAudio/ALSA/AAudio; Web Audio on wasm). Clips are cooked
`vaudio` assets loaded through the asset service and registered with miniaudio's
resource manager. If no audio device is available (headless/CI) the system runs
in an audio-less mode where every call is a safe no-op and `backendReady()`
returns `false`.

### Spatialization

`AudioSource` / `AudioListener` poses are synced in `onPreRender`, after the
world system has refreshed `TransformComponent::worldMatrix`, giving **3D
spatial audio with distance attenuation**. The active listener is whichever
entity has an `AudioListenerComponent` with `primary = true` (first wins); with
no listener present the engine keeps a default pose at the origin. In practice
the listener typically rides the active camera.

### Components

- **`AudioSourceComponent`** — `clip` (UUID of a `vaudio` asset), `volume`,
  `pitch`, `loop`, `playOnStart`, `playing`, `spatial` (when `false` the clip
  plays as a plain 2D/UI sound), and 3D falloff params `minDistance`,
  `maxDistance`, `rolloff`. The component stores desired state; runtime sound
  instances live in `AudioSystem`, which reconciles them every frame (including
  stopping sounds of destroyed entities).
- **`AudioListenerComponent`** — `primary` (bool).

### Behaviors

- **One-shots:** `playOneShot` (2D) and `playOneShotAt` (spatialized at a world
  position), with optional volume/pitch; return a `SoundId`.
- **Music:** `playMusic` (looping by default with optional fade-in) /
  `stopMusic(fadeOutMs)`.
- **Instance control by `SoundId`:** `stop` (with fade-out), `pause`, `resume`,
  `setVolume`, `setPitch`, `setLooping`, `isPlaying`.
- **Entity-driven playback:** `play` / `pause` / `stop` by `entt::entity` using
  the entity's `AudioSourceComponent`.
- **Global:** `setMasterVolume` / `masterVolume`.

> Lua API: see the **Audio** section of [Lua scripting](lua_scripting.md) — the
> global `Audio` table (`playOneShot`, `playOneShotAt`, `playMusic`, instance
> control, master volume) plus `entity.audioSource` / `entity.audioListener`.

---

## Animation

**Built on:** [ozz-animation](https://github.com/guillaumeblanc/ozz-animation)
runtime (`ozz::animation::Skeleton` / `Animation`), wrapped by `AnimationSystem`
(`source/vultra/include/vultra/function/animation/animation_system.hpp`).
Skeletons and animations are cooked `vasset` assets resolved through the asset
service and cached per UUID. The skin palette feeds skinned-mesh rendering.

### Component

**`AnimatorComponent`** drives skeletal animation in one of two modes:

- **`mode 0` (Single Clip):** plays the `animation` clip directly. Fields:
  `playOnStart`, `playing`, `loop`, `speed`, `time`.
- **`mode 1` (Graph):** runs an animator graph at `graph` (a
  `res://...vanimgraph.json`) — a state machine.

`skeleton` is optional; unset, it defaults to the entity's (or a descendant's)
skinned-mesh bundled skeleton.

### Animator state machine

The graph format lives in
`source/vultra/include/vultra/function/animation/animator_graph.hpp` and there is
a node-based animator graph editor. A graph contains:

- **Parameters** — typed `eFloat`, `eBool`, or `eTrigger` (one-shot, auto-resets
  after a transition consumes it), each with a default value.
- **States** — each names a clip `animation`, with `speed`, `loop`, and outgoing
  `transitions`.
- **Transitions** — a destination state, a list of `conditions` (ANDed; compare
  a parameter via greater/less/equal/notEqual/true/false/trigger), a cross-fade
  `duration`, and optional `hasExitTime` / `exitTime` (normalized source clip
  position). A graph also has `anyTransitions` ("Any State") evaluated from any
  state.

Per-entity controller runtime tracks current/target state, blend time, and live
parameter values; `controllerState` / `playbackState` expose them. The system
provides `setFloat` / `setBool` / `setTrigger` / `getFloat` / `getBool` to drive
parameters at runtime.

### Not exposed yet

The runtime is single-skeleton clip playback with state-machine cross-fade
blending. **Avatar masks (per-bone blend masks) and retargeting across
skeletons are not implemented** — graph blending is a global cross-fade between
two clips on the same skeleton.

> Lua API: see the **Animation** section of [Lua scripting](lua_scripting.md) —
> `entity.animator` and the global `Animation` table (playback control plus the
> controller parameter setters/getters and `Animation.currentState`).

---

## In-game UI

**Built on:** `UiSystem`
(`source/vultra/include/vultra/function/ui/ui_system.hpp`) with components in
`source/vultra/include/vultra/function/world/components/ui_components.hpp`. This
is the **runtime, in-world game UI** — distinct from Dear ImGui, which Vultra
uses only for editor and debug overlays. Each frame the system rebuilds resolved
rects from the canvas/RectTransform hierarchy and runs pointer input.

### Canvas and layout

- **`CanvasComponent`** — `enabled`, `sortOrder`, `referenceResolutionPx`,
  `scaleMode` (0 constant pixel size, 1 scale with screen),
  `renderMode` (0 screen overlay, 1 world-space — the canvas follows the entity
  transform in 3D), and `pixelsPerUnit` (world-space only). All child UI sizes
  are pixels relative to the reference resolution, scaled per `scaleMode`.
- **`RectTransformComponent`** — `anchorMin` / `anchorMax` (anchor rect),
  `pivot`, `anchoredPositionPx`, `sizeDeltaPx`, `rotation`, `scale`.

### Widgets

- **`UiPanelComponent`** — solid rounded panel: `color`, `borderRadiusPx`.
- **`UiImageComponent`** — `texture` (UUID), `tint`, `fitMode`
  (0 stretch / 1 contain / 2 cover).
- **`UiTextComponent`** — `text`, `color`, `fontSizePx`, `horizontalAlign`
  (left/center/right), `verticalAlign` (top/middle/bottom), and an optional
  `font` (project font UUID or a builtin font; empty = default builtin).
- **`UiButtonComponent`** — `interactable`, `targetGraphic`,
  `normalColor` / `hoveredColor` / `pressedColor`, plus system-updated
  `hovered` / `pressed` / `clicked` state.
- **`UiToggleComponent`** — `interactable`, `checked`, `offColor` / `onColor` /
  `checkColor`.
- **`UiSliderComponent`** — `interactable`, `value`, `minValue`, `maxValue`,
  `trackColor` / `fillColor` / `handleColor`.
- **`UiProgressBarComponent`** — `value`, `minValue`, `maxValue`,
  `trackColor` / `fillColor` (display-only, non-interactive).
- **`UiLayoutComponent`** — auto-layout of children: `kind`
  (0 none / 1 horizontal / 2 vertical / 3 grid), `paddingPx`, `marginPx`,
  `spacingPx`, `cellSizePx`.

### Pointer input

`UiSystem` raycasts the pointer against resolved rects each frame and tracks the
hovered and pressed entity. `pointerOverUi()`, `hoveredEntity()`,
`pressedEntity()`, `buttonClicked()`, `raycast(screenPx)`, and
`raycastCanvas(canvas, canvasPx)` expose the results. It emits hover/press/click
pointer events (`eventsThisFrame`) that scripts can subscribe to. For embedded
viewports (the editor Game View), `setInputViewport` overrides the mouse source.

> Lua API: see the **UI** section of [Lua scripting](lua_scripting.md) — the
> global `UI` table (`isPointerOverUI`, `hoveredEntity`, `raycast`, `events`),
> `self.rectTransform`, and per-widget references (`uiButton`, `uiToggle`,
> `uiSlider`, `uiProgressBar`) with signal-based events like
> `onClick:connect(...)`.
