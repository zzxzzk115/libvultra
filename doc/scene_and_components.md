# Scene & Components

**English** | [简体中文](zh_CN/scene_and_components_CN.md)

Vultra organizes everything in a level as a **scene**: a tree of **entities**, each
carrying **components**. This document covers the ECS, the `.vscn` file format,
how components are registered through reflection, and the full component catalog.

See also: [Lua scripting](lua_scripting.md) for driving entities/components at
runtime, and [Particle system](particle_system.md) for the particle backends.

## ECS overview

The world is built on [EnTT](https://github.com/skypjack/entt). The runtime type
is `vultra::World` (`source/vultra/include/vultra/function/world/world.hpp`),
which wraps a single `entt::registry`. An **entity** is a lightweight handle
(`entt::entity`); its data lives in the **components** attached to it.

`World` owns the hierarchy invariants. Parent/child links are stored in an
intrusive `HierarchyComponent` (`parent`, `firstChild`, `nextSibling`,
`prevSibling`, `childCount`), and structural edits go through `World`:

- `createEntity()` / `destroyEntity(e)`
- `createChild(parent)` / `destroyRecursive(root)`
- `setParent`, `removeParent`, `insertBefore`, `insertAfter`
- iteration: `firstChild(e)`, `nextSibling(e)`, `parent(e)`

`WorldSystem` (an `EngineSubsystem`, `source/vultra/.../world/world_system.cpp`)
creates the main world on init, provides it as `IWorldService`, and is **ticked**
once per frame via `onPreRender()`. Its job there is `updateWorldTransforms()`:
it walks the hierarchy depth-first from the roots and, for any entity whose
`TransformComponent` is `dirty` (or whose parent changed), recomputes
`worldMatrix = parentWorld * T * R * S`. The local matrix is built from
`position`, `rotation` (quaternion), and `scale`. `worldMatrix`/`dirty` are
runtime caches and are not serialized.

Gameplay/render behavior lives in other subsystems that iterate registry views
(physics, audio, animation, rendering, scripting). Components are plain data;
systems supply the logic.

## The `.vscn` scene format

Scenes serialize to `.vscn`, a human-readable INI/TOML-like text file. It is
intentionally diffable and version-control friendly: stable ordering, one field
per line, no binary blobs. Files are produced by `VscnWriter` and parsed by
`VscnReader` (`source/vultra/src/function/scene/`).

```ini
[vscn]
version = 1
root    = 0

[node id=1 name="Camera" parent=0 uuid="0a1b2c3d-...."]
NameComponent/name = "Camera"
TransformComponent/position = (0, 1.5, 6)
TransformComponent/rotation = (0, 0, 0, 1)
CameraComponent/primary = true
CameraComponent/fovY = 60

[node id=2 name="Sun" parent=0 uuid="9f8e7d6c-...."]
NameComponent/name = "Sun"
TransformComponent/rotation = (-0.27, 0.27, 0.07, 0.92)
LightComponent/kind = 0
LightComponent/intensity = 8
```

### Structure

- **`[vscn]` header** — `version` (currently `1`) and `root`. `root = 0` means
  the file uses a synthetic root: every top-level `[node]` is a real scene root
  whose `parent` is `0` (the virtual root). `root = 1` means node id `1` is the
  document root itself.
- **`[node ...]` lines** — one per entity, with attributes:
  - `id` — a file-local integer used only to express parenting within this file.
  - `name` — display name (optional).
  - `parent` — the `id` of the parent node, or `0` for a root.
  - `uuid` — the entity's stable `IDComponent` UUID (persistent identity across
    saves, referenced by prefabs/links).
  - `prefab` — optional prefab source URI for prefab instances.
- **`Component/field = value` lines** — the component data for the node above.
  The key is `<ComponentType>/<fieldName>`; the value is typed by the field:
  - strings: quoted, e.g. `"Camera"`
  - bools: `true` / `false` (also accepts `1` / `0`)
  - numbers: `60`, `0.1`
  - vectors/quaternions: parenthesized tuples, `(x, y, z)` / `(x, y, z, w)`
  - UUIDs: quoted UUID string

Values are mapped back onto component fields by name through reflection, so the
reader tolerates field renames as long as a legacy alias is registered (for
example `CameraComponent/fovYDegrees` still loads onto `fovY`).

## Component reflection

Reflection is the single mechanism behind serialization, the inspector, and the
add-component menu. `registerSceneMeta()`
(`source/vultra/src/function/scene/scene_reflection.cpp`) registers every
serializable component and field with EnTT's meta system:

```cpp
entt::meta_factory<TransformComponent>()
    .type("TransformComponent"_hs)
    .data<&TransformComponent::position>("position"_hs)
    .data<&TransformComponent::rotation>("rotation"_hs)
    .data<&TransformComponent::scale>("scale"_hs);
```

Because the `.vscn` key is exactly `TypeName/fieldName`, the same registration
table drives writing (enumerate fields), reading (resolve a key to a field and
parse its value), and the editor's inspector/add-component UI. Adding a new
serializable field is therefore a single edit: declare it on the component
struct and add one `.data<...>(...)` line here. Helper meta types (`glm::vec3`,
`glm::quat`, `CoreUUID`, material property/override structs) are registered
too so nested values round-trip.

## Component catalog

Every component below is a plain struct under
`source/vultra/include/vultra/function/world/components/`. Components marked
`VBIND_USERTYPE` are also exposed to Lua (see [Lua scripting](lua_scripting.md)).

### Core

- **NameComponent** — display name. Fields: `name`.
- **IDComponent** — stable per-entity UUID. Fields: `uuid`.
- **EntityStatusComponent** — editor/runtime flags: `active`, `visible`,
  `locked`, `selectable`.
- **LayerComponent** — render/culling layer bitmask. Fields: `mask`.
- **TransformComponent** — local `position`, `rotation` (quat), `scale`; plus a
  non-serialized `worldMatrix`/`dirty` cache.
- **HierarchyComponent** — intrusive parent/child tree links; managed by `World`,
  not edited directly.

### Rendering

- **CameraComponent** — `primary`, `projection` (0 perspective / 1 ortho),
  `fovY`, `orthographicHeight`, `zNear`, `zFar`, `clearMode`, `clearColor`,
  `priority`, `cullingMask`, `rendererKey`.
- **LightComponent** — `kind` (0 directional, 1 point, 2 spot, 3 area), `color`,
  `intensity`, `range`, `radius`, `width`/`height` (area), `innerConeDegrees`/
  `outerConeDegrees` (spot), `castsShadow`, `twoSided`.
- **MeshComponent** — `mesh` (UUID), `builtinGeometry` (UINT32_MAX = imported
  mesh, else 0 quad / 1 cube / 2 sphere / 3 capsule), and `materialOverrides`
  (per-slot material/graph + property block).
- **EnvironmentComponent** — scene sky/ambient/IBL: `active`, `skybox`,
  `ambientColor`, `ambientIntensity`, `enableIBL`, `iblColor`, `iblIntensity`.
- **ReflectionProbeComponent** — `active`, `enableIBL`, `environmentMap`,
  `shape` (0 box / 1 sphere), `boxSize`, `radius`, `blendDistance`, `intensity`,
  `priority`, `parallaxCorrection`.
- **ParticleEmitterComponent** — GPU or CPU billboard particles: `playing`,
  `worldSpace`, `gpu`, `maxParticles`, `emissionRate`, lifetime/spawn/velocity/
  gravity, and start/end size & color. See [Particle system](particle_system.md).
- **GaussianSplatComponent** — references a 3D Gaussian-splat asset. Fields:
  `gaussianSplat`.

### Physics

- **RigidBodyComponent** — Jolt-backed body: `motionType` (0 static / 1
  kinematic / 2 dynamic), `objectLayer`, `isSensor`, `motionQuality`,
  `allowSleeping`, `friction`, `restitution`, `linearDamping`, `angularDamping`,
  `gravityFactor`, `linearVelocity`, `angularVelocity`, `mass`/`overrideMass`,
  `maxLinearVelocity`, `maxAngularVelocity`. (Move bodies via `linearVelocity`.)
- **BoxShapeComponent** — `halfExtents`.
- **SphereShapeComponent** — `radius`.
- **CapsuleShapeComponent** — `halfHeightOfCylinder`, `radius`.
- **CylinderShapeComponent** — `halfHeight`, `radius`.
- **MeshShapeComponent** — collider from the entity's mesh; `convex` (false =
  static triangle mesh, true = convex hull usable on dynamic bodies).
- **CharacterControllerComponent** — kinematic character: `radius`, `height`,
  `maxSlopeAngleDegrees`, `stepHeight`, `gravityFactor`, `mass`, `jumpSpeed`,
  `objectLayer`, plus runtime `inputMove`, `jumpRequested`, `velocity`,
  `grounded`.

### Audio

- **AudioSourceComponent** — plays a cooked clip: `clip`, `volume`, `pitch`,
  `loop`, `playOnStart`, `playing`, `spatial`, `minDistance`, `maxDistance`,
  `rolloff`.
- **AudioListenerComponent** — marks the listener pose. Fields: `primary`.

### Animation

- **AnimatorComponent** — skeletal animation: `mode` (0 single clip / 1 animator
  graph), `skeleton`, `animation`, `playOnStart`, `playing`, `loop`, `speed`,
  `time`, `graph` (`.vanimgraph.json` URI for graph mode).

### UI

UI entities live under a canvas and use `RectTransformComponent` for layout.

- **CanvasComponent** — `enabled`, `sortOrder`, `referenceResolutionPx`,
  `scaleMode`, `renderMode` (0 screen overlay / 1 world space), `pixelsPerUnit`.
- **RectTransformComponent** — `anchorMin`/`anchorMax`, `pivot`,
  `anchoredPositionPx`, `sizeDeltaPx`, `rotation`, `scale`.
- **UiPanelComponent** — `enabled`, `color`, `borderRadiusPx`.
- **UiImageComponent** — `enabled`, `texture`, `tint`, `fitMode`.
- **UiTextComponent** — `enabled`, `text`, `color`, `fontSizePx`,
  `horizontalAlign`, `verticalAlign`, `font`.
- **UiButtonComponent** — `enabled`, `interactable`, `targetGraphic`, and
  normal/hovered/pressed colors (plus runtime hovered/pressed/clicked flags).
- **UiToggleComponent** — `enabled`, `interactable`, `checked`, off/on/check
  colors.
- **UiSliderComponent** — `enabled`, `interactable`, `value`, `minValue`,
  `maxValue`, track/fill/handle colors.
- **UiProgressBarComponent** — `enabled`, `value`, `minValue`, `maxValue`,
  track/fill colors.
- **UiLayoutComponent** — auto-layout: `enabled`, `kind` (0 none / 1 horizontal /
  2 vertical / 3 grid), `paddingPx`, `marginPx`, `spacingPx`, `cellSizePx`.

### Scripting & misc

- **ScriptComponent** — attaches a Lua script: `scriptUri` (engine URI),
  `enabled`. See [Lua scripting](lua_scripting.md).
- **XRViewComponent** — XR/stereo view config: `enabled`, `trackingOrigin`,
  `stereoGraphMode`, `fallbackMono`.

> The following are runtime/internal and not part of the reflected catalog above:
> `PrefabInstanceComponent` (prefab linkage) and `SkinPaletteComponent` (computed
> skinning matrices).

## Authoring

### In the editor

Create entities from the scene hierarchy panel (right-click -> add entity / child),
then attach components via the inspector's **Add Component** menu. The menu and
inspector are both generated from the reflection table, so any component
registered in `registerSceneMeta()` shows up automatically. Editing a field in
the inspector writes it straight onto the component; saving the scene serializes
through the same reflection into `.vscn`.

### From Lua

At runtime, spawn and destroy entities with the `World` API:

```lua
local e = World.create("Runtime Entity")
-- attach/configure components via their accessors, then:
World.destroy(e)
```

Components exposed with `VBIND_USERTYPE` (camera, light, environment, audio,
particle emitter, reflection probe, physics shapes, etc.) are reachable through
their script accessors. See [Lua scripting](lua_scripting.md) and the normative
[Lua API design](lua_api_design.md) for naming, units, and conventions.
