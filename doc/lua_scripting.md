# Vultra Lua Scripting

Vultra gameplay scripts are Lua files referenced by `ScriptComponent::scriptUri`.
They run only while editor/runtime playback is active. Scripts are loaded through
the asset system, so use engine URIs such as `res://scripts/player.lua` instead
of raw filesystem paths.

## Lifecycle

Define any of these functions in a script:

```lua
function OnCreate(self)
end

function OnUpdate(self, dt)
end

function OnFixedUpdate(self, fixedDt)
end

function OnDestroy(self)
end
```

`self` is the entity that owns the `ScriptComponent`.

- `OnCreate` is called when playback starts or the script is loaded/reloaded.
- `OnUpdate` is called once per rendered frame while playback is not paused.
- `OnFixedUpdate` is called once for each physics fixed step.
- `OnDestroy` is called when the script instance is destroyed.

Entity handles are lightweight C++ references. Always check `entity.valid`
before using cached handles across frames or after scene reloads.

## Binding Parity For Engine Work

Gameplay-facing engine work should leave a usable Lua surface behind. When a
system, component, service, or editor-authored behavior becomes relevant to
players:

- decide whether gameplay scripts need access to it;
- check nearby existing subsystem features for missing bindings;
- implement missing runtime service or data-layer behavior before exposing Lua;
- bind thin wrappers that call engine services/components;
- update this document and the project-local `ai/` notes used by generated
  games.

## Value Types

Use constructors for vector values:

```lua
local a = vec3(1, 2, 3)
local b = Vec3(4, 5, 6)
```

Vector bindings expose fields:

- `Vec2`: `x`, `y`
- `Vec3`: `x`, `y`, `z`
- `Vec4`: `x`, `y`, `z`, `w`

Vectors support basic arithmetic:

- `a + b`
- `a - b`
- `-a`
- `v * scalar`
- `scalar * v`
- `v / scalar`

Math helpers:

- `dot(a, b)`
- `lengthSquared(v)`

Use `lengthSquared` for distance checks when you do not need an exact square
root:

```lua
local delta = pickup.transform.position - player.transform.position
if lengthSquared(delta) < 1.0 then
  pickup.visible = false
end
```

## World And Entities

The `World` table exposes basic entity lookup and component creation:

```lua
local player = World.findByName("Player")
local pickups = World.findByNamePrefix("Pickup")
local spawned = World.create("Runtime Entity")
World.destroy(spawned)
```

Available functions:

- `World.create(name?)`
- `World.destroy(entity)`
- `World.count()`
- `World.findByName(name)`
- `World.findByNamePrefix(prefix)`
- `World.entities()`
- `World.addRigidBody(entity)` / `World.removeRigidBody(entity)`
- `World.addCamera(entity)` / `World.removeCamera(entity)`
- `World.addLight(entity)` / `World.removeLight(entity)`
- `World.addMesh(entity)` / `World.removeMesh(entity)`
- `World.addBoxShape(entity)` / `World.removeBoxShape(entity)`
- `World.addSphereShape(entity)` / `World.removeSphereShape(entity)`

Entity properties:

- `entity.valid`
- `entity.id`
- `entity.name`
- `entity.active`
- `entity.visible`
- `entity.transform`
- `entity.rigidBody`
- `entity.camera`
- `entity.light`
- `entity.mesh`
- `entity.boxShape`
- `entity.sphereShape`
- `entity.rectTransform`
- `entity.uiButton`
- `entity.uiToggle`
- `entity.uiSlider`
- `entity.uiProgressBar`

Entity methods use `:` syntax:

```lua
entity:destroy()
entity:setParent(parent)

local parent = entity:parent()
local child = entity:firstChild()
local sibling = entity:nextSibling()
```

Component checks:

- `entity:hasRigidBody()`
- `entity:hasCamera()`
- `entity:hasLight()`
- `entity:hasMesh()`
- `entity:hasBoxShape()`
- `entity:hasSphereShape()`
- `entity:hasRectTransform()`
- `entity:hasUiButton()`
- `entity:hasUiToggle()`
- `entity:hasUiSlider()`
- `entity:hasUiProgressBar()`

## Mesh Materials

Mesh scripts can assign a mesh slot material asset and per-entity property
overrides without editing the shared `.vmat.json`:

```lua
self.mesh:setMaterial(0, "res://materials/red.vmat.json")
self.mesh:setMaterialFloat(0, "roughness", 0.8)
self.mesh:setMaterialColor(0, "baseColor", vec4(1, 0, 0, 1))
self.mesh:setMaterialTexture(0, "baseColorTexture", "res://textures/albedo.png")
self.mesh:clearMaterialProperty(0, "roughness")
self.mesh:clearMaterialProperties(0)
```

`setMaterial` expects a `.vmat.json` URI. The property methods write the
slot-level `MaterialPropertyBlock`, so two entities can share one material asset
while using different runtime values. Lua writes affect the current
entity/component state and do not mutate material assets; Inspector-authored
blocks are scene authoring data and persist when the scene is saved.

## Transforms And Cameras

Transform access:

```lua
local p = self.transform.position
self.transform.position = vec3(p.x, p.y + 1, p.z)
self.transform.scale = vec3(1, 1, 1)
self.transform.rotationEuler = vec3(0, 45, 0)
self.transform:translate(vec3(0, 0, 1))
self.transform:setEulerDegrees(vec3(0, 90, 0))
self.transform:lookAt(vec3(0, 0, 0))
```

If an entity has `RectTransformComponent`, it is treated as a UI entity.
`entity.transform.position`, `scale`, `rotationEuler`, `translate`, and
`setEulerDegrees` forward to RectTransform pixel fields instead of the hidden
3D `TransformComponent`.

## UI

Runtime UI V1 is screen-space and uses pixels authored relative to the owning
Canvas reference resolution. RectTransform, layout spacing, padding, margins,
text size, and button hit rects are pixel values before Canvas scaling.

RectTransform access:

```lua
local rect = self.rectTransform
rect.anchoredPositionPx = vec2(320, 180)
rect.sizeDeltaPx = vec2(240, 64)
rect.rotationDegrees = 0
rect.scale = vec2(1, 1)
```

Button and pointer state:

```lua
if UI.isPointerOverUI() then
  local hovered = UI.hoveredEntity()
end

if self.uiButton.clickedThisFrame then
  print("Clicked")
end
```

Signal-based UI events are the recommended authoring style:

```lua
function OnCreate(self)
  self.uiButton.clicked:connect(function(event)
    print("Clicked", event.target.name)
  end)

  self.ui.onPointerEnter:connect(function(event)
    print("Pointer entered", event.currentTarget.name)
  end)

  self.ui.onClick:connect(function(event)
    event:stopPropagation()
  end)
end
```

Common UI controls expose thin component references:

```lua
function OnCreate(self)
  if self:hasUiToggle() then
    self.uiToggle.checked = true
    self.uiToggle.clicked:connect(function(event)
      print("toggle", self.uiToggle.checked)
    end)
  end

  if self:hasUiSlider() then
    self.uiSlider.minValue = 0
    self.uiSlider.maxValue = 100
    self.uiSlider.value = 50
  end

  if self:hasUiProgressBar() then
    self.uiProgressBar.value = 0.5
  end
end
```

UI signals bubble from the hit target through its parents up to the owning
Canvas. `event.target` is the original hit entity, `event.currentTarget` is the
entity whose signal is currently running, and `event:stopPropagation()` prevents
later parent signals for the same source event.

UI functions:

- `UI.isPointerOverUI()`
- `UI.hoveredEntity()`
- `UI.pressedEntity()`
- `UI.raycast(screenPosition?)`
- `UI.events()`

UI component references:

- `RectTransform`: `anchorMin`, `anchorMax`, `pivot`, `anchoredPositionPx`,
  `sizeDeltaPx`, `scale`, `rotationDegrees`
- `UiButton`: `interactable`, `hovered`, `pressed`, `clicked`,
  `clickedThisFrame`
- `UiToggle`: `interactable`, `checked`, `clicked`
- `UiSlider`: `interactable`, `value`, `minValue`, `maxValue`
- `UiProgressBar`: `value`, `minValue`, `maxValue`
- `Ui`: `onPointerEnter`, `onPointerExit`, `onPointerMove`, `onPointerDown`,
  `onPointerUp`, `onClick`

UI event fields are `type`, `target`, `currentTarget`, `canvas`,
`screenPosition`, `canvasPosition`, `localPosition`, `button`, `clickCount`,
and `handled`.

Primary camera lookup:

```lua
local camera = Camera.findPrimary()
if camera.valid then
  camera.transform.position = vec3(0, 6, 8)
  camera.transform:lookAt(self.transform.position)
end
```

Camera component properties:

- `camera.camera.valid`
- `camera.camera.primary`
- `camera.camera.projection`
- `camera.camera.fovYDegrees`
- `camera.camera.orthographicHeight`
- `camera.camera.cullingMask`
- `camera.camera.rendererKey`

Layer mask constants are available through `Layer.Default`, `Layer.UI`, and
`Layer.All`.

## Input

Keyboard and mouse input are exposed through `Input`.

```lua
if Input.getKey(KeyCode.W) then
  print("W is held")
end

if Input.getKeyDown(KeyCode.Space) then
  print("Space pressed this frame")
end
```

Keyboard functions:

- `Input.getKey(key)`
- `Input.getKeyDown(key)`
- `Input.getKeyUp(key)`
- `Input.getKeyRepeat(key)`

Mouse functions:

- `Input.getMouseButton(button)`
- `Input.getMouseButtonDown(button)`
- `Input.getMouseButtonUp(button)`
- `Input.getMouseButtonClicks(button)`
- `Input.getMousePosition()`
- `Input.getMousePositionFlipY()`
- `Input.getMousePositionDelta()`
- `Input.getMouseScrollDelta()`

Use `KeyCode` and `MouseCode` enum values rather than integer literals.

## Physics

Rigid bodies are controlled with the `RigidBody` reference on an entity or the
global `Physics` table.

```lua
function OnFixedUpdate(self, fixedDt)
  if not self:hasRigidBody() then
    return
  end

  self.rigidBody:addForce(vec3(10, 0, 0))
  self.rigidBody:activate()
end
```

Rigid body properties:

- `rigidBody.valid`
- `rigidBody.linearVelocity`
- `rigidBody.angularVelocity`
- `rigidBody.motionType`
- `rigidBody.objectLayer`
- `rigidBody.isSensor`
- `rigidBody.motionQuality`
- `rigidBody.allowSleeping`
- `rigidBody.mass`
- `rigidBody.overrideMass`
- `rigidBody.friction`
- `rigidBody.restitution`
- `rigidBody.linearDamping`
- `rigidBody.angularDamping`
- `rigidBody.gravityFactor`
- `rigidBody.maxLinearVelocity`
- `rigidBody.maxAngularVelocity`

Rigid body methods:

- `rigidBody:activate()`
- `rigidBody:addForce(force)`
- `rigidBody:addImpulse(impulse)`

Physics table:

- `Physics.enabled()`
- `Physics.setEnabled(enabled)`
- `Physics.bodyCount()`
- `Physics.hasBody(entity)`
- `Physics.fixedTimeStep()`
- `Physics.setFixedTimeStep(seconds)`
- `Physics.addForce(entity, force)`
- `Physics.addImpulse(entity, impulse)`
- `Physics.addTorque(entity, torque)`
- `Physics.addAngularImpulse(entity, impulse)`
- `Physics.setPosition(entity, position, activate?)`
- `Physics.setRotation(entity, eulerDegrees, activate?)`
- `Physics.gravity()` / `Physics.setGravity(vec3)`
- `Physics.setLayerCollision(layerA, layerB, enabled)` / `Physics.layerCollision(layerA, layerB)`
- `Physics.raycast(origin, direction, maxDistance, activeOnly?, layerMask?)`
- `Physics.raycastAll(origin, direction, maxDistance, activeOnly?, layerMask?)` — array of hits, near→far
- `Physics.sphereCast(origin, direction, radius, maxDistance, activeOnly?, layerMask?)` — swept sphere
- `Physics.overlapSphere(center, radius, activeOnly?, layerMask?)`
- `Physics.overlapBox(center, halfExtents, activeOnly?, layerMask?)`
- `Physics.overlapCapsule(center, halfHeight, radius, activeOnly?, layerMask?)`
- `Physics.contactPairs(activeOnly?)`
- `Physics.contactEvents()` — drain contact/trigger events since the last call

Queries now use real Jolt narrow-phase geometry (not AABB approximations) and
return accurate hit points/normals. `layerMask` is a 32-bit mask over logical
collision-layer indices (`RigidBodyComponent.objectLayer`): bit `i` set means
"include bodies on layer `i`". Use it to scope shots/queries (e.g. ignore the
player layer). `Physics.sphereCast` returns a table `{hit, entity, point, normal,
distance, fraction, startPenetrating}`.

### Character controller

FPS/TPS movement uses a `CharacterControllerComponent` (Jolt `CharacterVirtual`):
collide-and-slide, slope limits, stair stepping, and ground detection. Drive it
through the `Character` table:

```lua
function OnFixedUpdate(self, fixedDt)
  local e = self.entity
  Character.move(e, vec3(Input.axisX() * 4.0, 0, Input.axisZ() * 4.0))
  if Input.isKeyPressed("space") and Character.isGrounded(e) then
    Character.jump(e, 6.0)
  end
end
```

- `Character.has(entity)`
- `Character.move(entity, horizontalVelocity)` — desired horizontal (x,z) velocity
- `Character.jump(entity, speed?)` — request a jump (uses component `jumpSpeed` if `speed` omitted/0)
- `Character.isGrounded(entity)`
- `Character.velocity(entity)` / `Character.groundNormal(entity)`
- `Character.setPosition(entity, position)` — teleport

`Physics.overlapSphere` returns a Lua array of active rigid-body entities whose
shape overlaps the query sphere. It currently supports sphere, box, and capsule
shape components. `activeOnly` defaults to `true`.

`Physics.raycast` returns a `PhysicsRaycastHit`:

- `hit`
- `entity`
- `point`
- `normal`
- `fraction`
- `distance`

`Physics.contactPairs` returns `PhysicsContactPair` values with `a` and `b`
entity fields. These are gameplay contact snapshots from the physics system.

`Physics.contactEvents()` returns and clears the discrete contact/trigger events
captured since the last call — an array of `{a, b, type, isSensor}` where `type`
is `"enter"` (new contact) or `"exit"` (contact ended). When `isSensor` is true the
event is a trigger volume enter/exit (one body has `rigidBody.isSensor = true`). Poll
this each frame for pickups, damage zones, and hit detection. Example:

```lua
function OnUpdate(self, dt)
  for _, e in ipairs(Physics.contactEvents()) do
    if e.isSensor and e.type == "enter" then
      -- something entered a trigger volume (e.a / e.b are the entities)
    end
  end
end
```

For dynamic rigid bodies, prefer `OnFixedUpdate` plus forces or impulses. Avoid
writing `Transform.position` every rendered frame on a dynamic body unless the
entity is intentionally kinematic or scripted outside the physics simulation.

## Roll-A-Ball Example

This example uses WASD to push a dynamic ball, follows it with the game camera,
and marks nearby pickup entities as collected. Pickups are expected to be named
with a `Pickup` prefix.

```lua
RollABall = {
  score = 0,
  won = false,
  moveForce = 45.0,
  pickupRadius = 1.25,
}

local function movementInput()
  local x = 0.0
  local z = 0.0

  if Input.getKey(KeyCode.A) then x = x - 1.0 end
  if Input.getKey(KeyCode.D) then x = x + 1.0 end
  if Input.getKey(KeyCode.W) then z = z - 1.0 end
  if Input.getKey(KeyCode.S) then z = z + 1.0 end

  return vec3(x, 0.0, z)
end

function OnCreate(self)
  RollABall.score = 0
  RollABall.won = false

  if self:hasRigidBody() then
    self.rigidBody.mass = 1.0
    self.rigidBody.overrideMass = true
    self.rigidBody.friction = 0.25
    self.rigidBody.restitution = 1.0
    self.rigidBody.gravityFactor = 1.0
  end

  print("[RollABall] started")
end

function OnFixedUpdate(self, fixedDt)
  if not self:hasRigidBody() then
    return
  end

  local input = movementInput()
  self.rigidBody:addForce(vec3(input.x * RollABall.moveForce, 0.0, input.z * RollABall.moveForce))
  self.rigidBody:activate()
end

function OnUpdate(self, dt)
  local playerPos = self.transform.position

  local camera = Camera.findPrimary()
  if camera.valid then
    camera.transform.position = vec3(playerPos.x, playerPos.y + 6.0, playerPos.z + 7.0)
    camera.transform:lookAt(playerPos)
  end

  local pickups = Physics.overlapSphere(playerPos, RollABall.pickupRadius)
  local remaining = 0
  for _, pickup in ipairs(pickups) do
    if pickup.valid and pickup.active and pickup.name:sub(1, 6) == "Pickup" then
      pickup.active = false
      pickup.visible = false
      RollABall.score = RollABall.score + 1
      print("[RollABall] pickup " .. tostring(RollABall.score))
    end
  end

  for _, pickup in ipairs(World.findByNamePrefix("Pickup")) do
    if pickup.valid and pickup.active then
      remaining = remaining + 1
    end
  end

  if remaining == 0 and not RollABall.won then
    RollABall.won = true
    print("[Lua] you win")
  end
end
```

If the visual mesh is an imported scene asset, keep gameplay state on a parent
entity and put the imported mesh under it as a child:

- Parent: `RigidBodyComponent`, `SphereShapeComponent`, `ScriptComponent`.
- Child: imported visual scene/mesh.

This keeps physics, scripting, and save behavior independent from external asset
scene roots.

## Assets And Scenes

Asset APIs:

- `Asset.resolveUri(uri)`
- `Asset.loadText(uri)`
- `Asset.loadMesh(uri)`
- `Asset.loadTexture(uri)`
- `Asset.loadGaussianSplat(uri)`
- `Asset.memoryStats()`

`Asset.loadText` returns a result with `ok`, `text`, and `error`. Mesh, texture,
and Gaussian splat loads return an `AssetHandle` with `valid`, `ready`, `uuid`,
`state`, and `gpuIndex`.

Scene APIs:

- `Scene.load(uri)`
- `Scene.instantiate(uri)`
- `Scene.instantiateChild(uri, parent, clearWorld)`
- `Scene.saveWorld(uri)`
- `Scene.saveEntity(uri, root)`

## Script And Render Utilities

Script APIs:

- `Script.reloadAll()`
- `Script.reloadEntity(entity)`
- `Script.hasInstance(entity)`
- `Script.destroyInstance(entity)`
- `Script.setPlaybackState(playing, paused)`
- `Script.isPlaybackPlaying()`
- `Script.isPlaybackPaused()`
- `Script.runString(code)`

Time APIs:

- `Time.deltaTime()`
- `Time.fixedDeltaTime()`
- `Time.unscaledDeltaTime()`
- `Time.smoothedDeltaTime()`
- `Time.totalTime()`
- `Time.unscaledTotalTime()`
- `Time.averageFrameTime()`
- `Time.framesPerSecond()`
- `Time.timeScale()`
- `Time.fixedAlpha()`
- `Time.fixedStepsThisFrame()`
- `Time.frameIndex()`
- `Time.maxDeltaTime()`
- `Time.setTimeScale(scale)`
- `Time.setFixedDeltaTime(dt)`
- `Time.setMaxDeltaTime(dt)`
- `Time.setMaxFixedStepsPerFrame(maxSteps)`
- `Time.setDeltaSmoothingFactor(factor)`

Render APIs:

- `Render.resize(width, height)`
- `Render.setProfilerEnabled(enabled)`
- `Render.isProfilerEnabled()`
- `Render.profilerHistorySize()`
- `Render.captureFrame()`
- `Render.getGaussianSplatSettings()`
- `Render.setGaussianSplatSettings(settings)`
- `Render.getGaussianSplatFrameStats()`
- `RenderBackend.isXREnabled()`
- `RenderBackend.isXRMirrorEnabled()`
- `RenderBackend.isExitRequested()`

## Animation

Animator playback can be controlled either through `entity.animator` or the
global `Animation` table.

```lua
local actor = World.findByName("Actor")
if actor.valid and actor:hasAnimator() then
  actor.animator:play(true)
  actor.animator.speed = 1.0
  actor.animator.loop = true

  local state = actor.animator:state()
  print("animation time " .. tostring(state.time))
end
```

Animator component properties:

- `animator.valid`
- `animator.skeleton`
- `animator.animation`
- `animator.playing`
- `animator.loop`
- `animator.speed`
- `animator.time`

Animator methods:

- `animator:play(restart?)`
- `animator:pause()`
- `animator:stop()`
- `animator:setNormalizedTime(normalizedTime)`
- `animator:state()`

Global animation APIs:

- `Animation.play(entity, restart?)`
- `Animation.pause(entity)`
- `Animation.stop(entity)`
- `Animation.setAnimation(entity, animationUuid, restart?)`
- `Animation.setTime(entity, seconds)`
- `Animation.setNormalizedTime(entity, normalizedTime)`
- `Animation.setSpeed(entity, speed)`
- `Animation.setLoop(entity, loop)`
- `Animation.state(entity)`
- `Animation.jointCount(skeletonUuid)`
- `Animation.duration(animationUuid)`

`Animation.state` returns an `AnimatorPlaybackState`:

- `valid`
- `playing`
- `loop`
- `speed`
- `time`
- `duration`
- `normalizedTime`
- `skeleton`
- `animation`

## Common Pitfalls

- Use `:` for bound methods such as `entity:destroy()` and
  `self.rigidBody:addForce(force)`.
- Check `entity.valid` and component-specific `valid` fields before using
  handles found by name.
- Accessing a missing component property may raise a Lua error. Prefer
  `entity:hasRigidBody()` and similar checks first.
- Use `OnFixedUpdate` for physics forces. Use `OnUpdate` for input sampling,
  camera follow, score checks, and non-physics animation.
- Collision event callbacks are not part of the current Lua API yet. For simple
  pickups and trigger volumes, use `Physics.overlapSphere`,
  `Physics.overlapBox`, or `Physics.contactPairs`.
- Animator playback is single-clip playback per `AnimatorComponent` for now.
  Blend trees, state machines, masks, and retargeting need dedicated animation
  controller data.
- Imported visual assets may have very small or very large source units. Verify
  scale in Game View after instantiation.
- Embedded textures inside GLB imports must be validated by rendered material
  output, not only by registry/import success.
