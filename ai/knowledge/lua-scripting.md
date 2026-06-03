# Gameplay Lua Scripting Knowledge

User-facing Lua scripting documentation lives in `doc/lua_scripting.md`. Read it
before generating gameplay scripts or AI-authored project content.

Stable facts for agents:

- Gameplay-facing engine changes require a binding parity pass: check whether
  players need Lua access, audit nearby same-subsystem gaps, add missing
  service/data implementation before the binding, then update this file,
  `doc/lua_scripting.md`, and affected project-local `ai/` docs.
- Gameplay scripts are loaded from `ScriptComponent::scriptUri` with engine URIs
  such as `res://scripts/player.lua`.
- Lifecycle callbacks are `OnCreate(self)`, `OnUpdate(self, dt)`,
  `OnFixedUpdate(self, fixedDt)`, and `OnDestroy(self)`.
- `self` is an `Entity` handle. Entity handles are C++ references; check
  `entity.valid` before using cached handles across frames or scene reloads.
- `vec2`, `vec3`, `vec4`, `Vec2`, `Vec3`, and `Vec4` expose fields and basic
  arithmetic: `+`, `-`, unary `-`, scalar `*`, and scalar `/`. Use
  `lengthSquared(v)` and `dot(a, b)` for cheap gameplay checks.
- Methods are called with `:` syntax: `entity:destroy()`,
  `entity:setParent(parent)`, `self.rigidBody:addForce(force)`,
  `self.transform:lookAt(target)`.
- Check component presence before component property access:
  `entity:hasRigidBody()`, `entity:hasCamera()`, `entity:hasMesh()`,
  `entity:hasBoxShape()`, `entity:hasSphereShape()`,
  `entity:hasRectTransform()`, `entity:hasUiButton()`,
  `entity:hasUiToggle()`, `entity:hasUiSlider()`, and
  `entity:hasUiProgressBar()`.
- UI APIs are screen-space and pixel-authored. `RectTransform` fields,
  layout spacing/padding/margins, text sizes, and button hit rects use Canvas
  reference-resolution pixels before Canvas scaling.
- If an entity has `RectTransformComponent`, `entity.transform` operations
  forward to RectTransform pixel fields; use `entity.rectTransform` for direct
  access to anchors, pivot, `anchoredPositionPx`, `sizeDeltaPx`, `scale`, and
  `rotationDegrees`. UI authoring should prefer signal callbacks:
  `entity.uiButton.clicked:connect(fn)` for buttons and
  `entity.uiToggle.clicked:connect(fn)` for toggles, plus
  `entity.ui.onPointerEnter/onPointerExit/onPointerMove/onPointerDown/onPointerUp/onClick`
  for general UI entities. Sliders expose `entity.uiSlider.value`,
  `minValue`, and `maxValue`; progress bars expose
  `entity.uiProgressBar.value`, `minValue`, and `maxValue`.
  `entity.uiButton.clickedThisFrame`,
  `UI.isPointerOverUI()`, `UI.raycast(screenPosition?)`, and `UI.events()` are
  also available through the runtime UI service.
- Physics-driven movement should use `OnFixedUpdate` and rigid-body
  `addForce`/`addImpulse`. Avoid writing `Transform.position` every rendered
  frame for dynamic rigid bodies.
- `Camera.findPrimary()` returns the game camera entity; camera follow scripts
  should update that camera in `OnUpdate` and call `transform:lookAt`. Camera
  scripts can read/write `camera.camera.cullingMask`; layer constants are
  exposed as `Layer.Default`, `Layer.UI`, and `Layer.All`.
- Mesh material scripting should use `entity.mesh:setMaterial(slot, uri)` for
  `.vmat.json` slot assignment and `setMaterialFloat`, `setMaterialColor`,
  `setMaterialTexture`, `clearMaterialProperty`, and
  `clearMaterialProperties` for per-entity MaterialPropertyBlock overrides.
  These calls update the current entity/component state and do not mutate shared
  `.vmat.json` assets; Inspector-authored blocks are scene authoring data. Use
  `materialColor` only for the legacy builtin primitive color shortcut.
- Lua exposes `Physics.overlapSphere(center, radius, activeOnly?)` through
  `IPhysicsService`, plus `raycast`, `overlapBox`, `contactPairs`, and
  `setPosition`. Pickup gameplay should prefer physics queries, then toggle
  `active`/`visible`, until event callbacks are exposed.
- Lua exposes `Animation` and `entity.animator` for single-clip animator
  playback: play, pause, stop, set time, set speed, set loop, state, duration,
  and joint count. Do not invent blend tree/state-machine APIs until the engine
  has controller data for them.
- Imported visual assets should usually be children of a gameplay parent entity
  that owns the rigid body, shape, and script.
- Imported GLB embedded textures are not considered verified until rendered
  material output is inspected; registry/import success alone is insufficient.
