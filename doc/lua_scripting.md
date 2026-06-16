# Vultra Lua Scripting

**English** | [简体中文](zh_CN/lua_scripting_CN.md)

Vultra gameplay scripts are Lua files referenced by `ScriptComponent::scriptUri`.
They run only while editor/runtime playback is active. Scripts are loaded through
the asset system, so use engine URIs such as `res://scripts/player.lua` instead
of raw filesystem paths.

> API authors: the Lua surface follows the normative rules in
> [lua_api_design.md](lua_api_design.md) (naming, properties vs methods, units,
> error/nil conventions, deprecation). New or changed bindings must keep
> `tests/lua_api_conformance` green.

## Lifecycle

Define any of these functions in a script:

```lua
function OnCreate(self)
end

function OnEnable(self)
end

function OnUpdate(self, dt)
end

function OnFixedUpdate(self, fixedDt)
end

function OnDisable(self)
end

function OnDestroy(self)
end
```

`self` is the entity that owns the `ScriptComponent`.

- `OnCreate` is called when playback starts or the script is loaded/reloaded.
- `OnEnable` is called after `OnCreate` when the script starts enabled, and
  whenever `ScriptComponent.enabled` flips back to true.
- `OnUpdate` is called once per rendered frame while playback is not paused.
- `OnFixedUpdate` is called once for each physics fixed step.
- `OnDisable` is called when the component is disabled, and always before
  `OnDestroy`. Coroutines owned by the script stop here.
- `OnDestroy` is called when the script instance is destroyed.

Guaranteed order: `OnCreate -> OnEnable -> updates... -> OnDisable -> OnDestroy`.

Entity handles are lightweight C++ references. Always check `entity.valid`
before using cached handles across frames or after scene reloads.

### Collision And Trigger Callbacks

Scripts on either entity of a physics contact receive callbacks, dispatched
once per physics update from contact-pair diffing (`other` is the colliding
entity):

```lua
function OnCollisionEnter(self, other)
end

function OnCollisionStay(self, other)
end

function OnCollisionExit(self, other)
end

function OnTriggerEnter(self, other) -- either body has isSensor = true
end

function OnTriggerStay(self, other)
end

function OnTriggerExit(self, other)
end
```

Polling via `Physics.contactEvents()` still works and is unaffected by these
callbacks. On exit events, `other` may already be invalid -- check
`other.valid` before touching it.

### Animation Event Callback

If the entity's animator graph defines keyframe events (see **Animation**), the script's
`OnAnimationEvent(self, name)` is called the frame playback crosses each event's time:

```lua
function OnAnimationEvent(self, name)
  if name == "footstep" then
    Audio.play(self) -- play a footstep SFX
  end
end
```

Events fire on the currently-playing graph state, loop with the clip, and reset across
transitions (no retroactive burst when a state is entered).

### Debug UI (ImGui)

In editor and dev builds (when the ImGui service exists), scripts can draw
debug UI through the `ImGui` table. It keeps upstream Dear ImGui PascalCase
names (the documented exception in [lua_api_design.md](lua_api_design.md)) so
upstream docs apply directly. Calls outside the ImGui frame raise an error;
in shipped headless runtimes the `ImGui` global is absent -- guard with
`if ImGui then`.

```lua
function OnUpdate(self, dt)
  if not ImGui then return end
  local visible = ImGui.Begin("Debug")
  if visible then
    ImGui.Text("entity: " .. self.name)
    if ImGui.Button("Reset") then
      self.transform.position = Vec3(0, 0, 0)
    end
    local changed, v = ImGui.SliderFloat("speed", Player.speed, 0, 10)
    if changed then Player.speed = v end
  end
  ImGui.End()
end
```

Out-parameters become extra return values (`changed, newValue`). For example,
`ImGui.InputFloat("speed", Player.speed)` and `ImGui.InputInt("count", n)`
return `changed, newValue`. Enum/flag values live in sub-tables:
`ImGui.WindowFlags.NoTitleBar`, `ImGui.Cond.Once`. The binding is generated from
dear_bindings metadata (`tools/python/gen_imgui_lua.py`).

### ImGui extensions (imgui-ext)

Several imgui-ext widgets are exposed as their own tables, keeping their upstream
PascalCase names (like `ImGui`). They are only present in editor/dev builds (an
ImGui service must be active). Matrices are Lua arrays of 16 numbers
(column-major); vec3s are arrays of 3.

- **`ImGuizmo`** — 3D transform gizmos: `Manipulate(view, projection, operation,
  mode, matrix[, snap]) -> changed, newMatrix`, plus `SetRect`, `SetDrawlist`,
  `SetOrthographic`, `Enable`, `IsOver`, `IsUsing`, `IsUsingAny`,
  `DecomposeMatrixToComponents`/`RecomposeMatrixFromComponents`. Operations live
  in `ImGuizmo.OPERATION` (`TRANSLATE`/`ROTATE`/`SCALE`/`UNIVERSAL`/…) and modes
  in `ImGuizmo.MODE` (`LOCAL`/`WORLD`).
- **`ImOGuizmo`** — orientation cube: `SetRect`, `BeginFrame`,
  `DrawGizmo(view, projection[, pivotDistance]) -> interacted, newView`.
- **`ImPlot`** — plotting: wrap in `BeginPlot(title[, sizeX, sizeY, flags])` /
  `EndPlot()`, then `PlotLine`/`PlotScatter`/`PlotBars(label, ys)` or
  `(label, xs, ys)`. Helpers: `SetupAxes`, `SetupAxesLimits`, `SetupLegend`.
  Enums: `ImPlot.Axis`, `ImPlot.Flags`, `ImPlot.AxisFlags`, `ImPlot.Location`.
- **`ImGuiFileDialog`** — modal file/folder picker: `OpenDialog(key, title, filters[,
  path])`, then each frame `Display(key)`; when it returns true, check `IsOk()` and
  read `GetFilePathName()` / `GetCurrentPath()` / `GetSelection()`, then `Close()`.
- **`ImNodes`** — node-graph editor: wrap in `BeginNodeEditor()`/`EndNodeEditor()`;
  per node `BeginNode(id)`/`EndNode()` with `BeginInputAttribute`/`BeginOutputAttribute`
  pins and `Link(id, startAttr, endAttr)`. Query interactions with
  `IsLinkCreated()`/`IsLinkDestroyed()` (each returns the relevant ids).

```lua
-- a tiny plot in an editor panel
if ImPlot.BeginPlot("Frame time (ms)", -1, 160) then
  ImPlot.SetupAxes("frame", "ms")
  ImPlot.PlotLine("dt", samples)   -- samples is a Lua array of numbers
  ImPlot.EndPlot()
end
```

### Coroutines

Each script can run coroutines that persist across frames:

```lua
function OnCreate(self)
  startCoroutine(function()
    print("first slice runs immediately")
    wait(1.5)            -- seconds
    print("1.5s later")
    waitFrames(10)       -- frames
    print("10 frames later")
  end)
end
```

- `startCoroutine(fn, ...)` runs the first slice immediately and returns the
  coroutine; extra arguments are passed to `fn`.
- `wait(seconds)` / `waitFrames(n)` suspend the current coroutine.
- `stopAllCoroutines()` cancels this script's coroutines; they also stop
  automatically on `OnDisable` and `OnDestroy`.

### Tween, Timer, Timeline

Frame-driven gameplay utilities (global tables, advanced once per frame by the script
system). Unlike coroutines they are global, not per-entity, so cancel handles you no longer
need and guard callbacks that touch entities (the entity may have been destroyed).

```lua
-- Tween numeric fields of any table toward targets over a duration.
local pos = self.transform.position
Tween.to(pos, { y = pos.y + 2 }, 0.5, {
  ease = Ease.cubicOut,
  onUpdate = function(t) self.transform.position = pos end, -- write back each frame
  onComplete = function() print("done") end,
})

-- One-shot and repeating timers.
Timer.after(2.0, function() print("2s later") end)
local h = Timer.every(0.5, function() spawnEnemy() end) -- return false from fn to stop
Timer.cancel(h)

-- A small ordered sequencer (cutscene-ish).
Timeline.new()
  :call(function() openDoor() end)
  :wait(1.0)
  :call(function() spawnBoss() end)
  :start()
```

- `Tween.to(obj, toFields, duration, opts?)` -> handle. `opts = { ease, onUpdate(t),
  onComplete }`. `Tween.cancel(handle)`.
- `Timer.after(seconds, fn)` / `Timer.every(seconds, fn)` (return `false` from `fn` to stop)
  -> handle. `Timer.cancel(handle)`.
- `Timeline.new():wait(s):call(fn):start()` -> handle (chainable).
- `Ease`: `linear`, `quadIn/Out/InOut`, `cubicIn/Out/InOut`, `sineIn/Out/InOut`,
  `expoIn/Out`, `backOut`, `bounceOut`.

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
local a = Vec3(1, 2, 3)
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

The `World` table exposes basic entity lookup and lifetime:

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

Entity properties (the only direct fields on a handle):

- `entity.valid`
- `entity.id`
- `entity.name`
- `entity.active`
- `entity.visible`
- `entity.transform`

Entity methods use `:` syntax:

```lua
entity:destroy()
entity:setParent(parent)

local parent = entity:parent()
local child = entity:firstChild()
local sibling = entity:nextSibling()
```

### Components

Every component other than the always-on `transform` is reached through a single
Unity-style generic API keyed by a `Component.*` token, rather than per-component
properties:

```lua
local body = self:addComponent(Component.RigidBody) -- adds if absent, returns the ref
local cam = self:getComponent(Component.Camera)     -- ref, or nil if absent
local had = self:removeComponent(Component.Light)   -- true if one was removed

if self:hasComponent(Component.Mesh) then
  self:getComponent(Component.Mesh):setMaterial(0, "res://materials/red.vmat.json")
end
```

- `entity:addComponent(Component.X)` adds the component if absent and returns its
  reference.
- `entity:getComponent(Component.X)` returns the component reference, or `nil`
  when the entity does not have it.
- `entity:removeComponent(Component.X)` returns `true` if a component was removed.
- `entity:hasComponent(Component.X)` returns a boolean.

The returned references are the same typed component refs documented in the
sections below (`RigidBody`, `Camera`, `Mesh`, `Animator`, ...); only how you
obtain them changed. `getComponent` returning `nil` is the idiomatic existence
check before use:

```lua
local body = self:getComponent(Component.RigidBody)
if body then
  body:addForce(Vec3(10, 0, 0))
end
```

`Component` is a global enum table of typed tokens:

- `Component.Transform`, `Component.RigidBody`, `Component.Camera`,
  `Component.Light`, `Component.Mesh`
- `Component.BoxShape`, `Component.SphereShape`, `Component.CapsuleShape`,
  `Component.CylinderShape`
- `Component.Animator`, `Component.AudioSource`, `Component.AudioListener`
- `Component.Environment`, `Component.ParticleEmitter`,
  `Component.ReflectionProbe`
- `Component.RectTransform`, `Component.UiButton`, `Component.UiToggle`,
  `Component.UiSlider`, `Component.UiProgressBar`

### WorldHelper

`WorldHelper` is a global table of convenience constructors that each create an
entity with common components already attached and return the new handle:

```lua
local empty = WorldHelper.addEmpty("Spawn")
local cam = WorldHelper.addMainCamera("Main Camera") -- camera with primary = true
```

- `WorldHelper.addEmpty(name?)` -- transform only
- `WorldHelper.addMainCamera(name?)` -- camera with `primary = true`
- `WorldHelper.addCamera(name?)`
- `WorldHelper.addLight(name?)`
- `WorldHelper.addMesh(name?)`

## Mesh Materials

Mesh scripts can assign a mesh slot material asset and per-entity property
overrides without editing the shared `.vmat.json`:

```lua
local mesh = self:getComponent(Component.Mesh)
mesh:setMaterial(0, "res://materials/red.vmat.json")
mesh:setMaterialFloat(0, "roughness", 0.8)
mesh:setMaterialColor(0, "baseColor", Vec4(1, 0, 0, 1))
mesh:setMaterialTexture(0, "baseColorTexture", "res://textures/albedo.png")
mesh:clearMaterialProperty(0, "roughness")
mesh:clearMaterialProperties(0)
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
self.transform.position = Vec3(p.x, p.y + 1, p.z)
self.transform.scale = Vec3(1, 1, 1)
self.transform.rotation = Vec3(0, 45, 0) -- euler degrees
self.transform:translate(Vec3(0, 0, 1))
self.transform:lookAt(Vec3(0, 0, 0))
```

If an entity has `RectTransformComponent`, it is treated as a UI entity.
`entity.transform.position`, `scale`, `rotation`, and `translate` forward to
RectTransform pixel fields instead of the hidden 3D `TransformComponent`.

## UI

Runtime UI V1 is screen-space and uses pixels authored relative to the owning
Canvas reference resolution. RectTransform, layout spacing, padding, margins,
text size, and button hit rects are pixel values before Canvas scaling.

RectTransform access (reach it with `Component.RectTransform`):

```lua
local rect = self:getComponent(Component.RectTransform)
rect.anchoredPositionPx = Vec2(320, 180)
rect.sizeDeltaPx = Vec2(240, 64)
rect.rotation = 0
rect.scale = Vec2(1, 1)
```

Button and pointer state:

```lua
if UI.isPointerOverUI() then
  local hovered = UI.hoveredEntity()
end

local button = self:getComponent(Component.UiButton)
if button and button.clickedThisFrame then
  print("Clicked")
end
```

Signal-based UI events are the recommended authoring style. The `onClick` signal
on a button reference bubbles from the hit target through its parents up to the
owning Canvas:

```lua
function OnCreate(self)
  local button = self:getComponent(Component.UiButton)
  if button then
    button.onClick:connect(function(event)
      print("Clicked", event.target.name)
      event:stopPropagation()
    end)
  end
end
```

Common UI controls expose thin component references reached the same way:

```lua
function OnCreate(self)
  local toggle = self:getComponent(Component.UiToggle)
  if toggle then
    toggle.checked = true
    toggle.onClick:connect(function(event)
      print("toggle", toggle.checked)
    end)
  end

  local slider = self:getComponent(Component.UiSlider)
  if slider then
    slider.minValue = 0
    slider.maxValue = 100
    slider.value = 50
  end

  local progress = self:getComponent(Component.UiProgressBar)
  if progress then
    progress.value = 0.5
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

UI component references (reached via `Component.RectTransform`,
`Component.UiButton`, `Component.UiToggle`, `Component.UiSlider`,
`Component.UiProgressBar`):

- `RectTransform`: `anchorMin`, `anchorMax`, `pivot`, `anchoredPositionPx`,
  `sizeDeltaPx`, `scale`, `rotation`
- `UiButton`: `interactable`, `hovered`, `pressed`, `clickedThisFrame`, `onClick`
- `UiToggle`: `interactable`, `checked`, `onClick`
- `UiSlider`: `interactable`, `value`, `minValue`, `maxValue`
- `UiProgressBar`: `value`, `minValue`, `maxValue`
- `UiInputField`: `text`, `placeholder`, `interactable`, `focused` (read-only),
  `submitted` (read-only), `onValueChanged`, `onSubmit`

The **UiInputField** is a single-line editable text field. The UI system owns focus (click to
focus), keyboard editing (typed text, Backspace/Delete, arrows/Home/End), and Enter→submit;
overflow is clipped to the rect.

```lua
local field = self:getComponent(Component.UiInputField)
field.placeholder = "Name..."
field.onValueChanged:connect(function() print("now:", field.text) end)
field.onSubmit:connect(function() print("submitted:", field.text) end)
```

UI event fields are `type`, `target`, `currentTarget`, `canvas`,
`screenPosition`, `canvasPosition`, `localPosition`, `button`, `clickCount`,
and `handled`.

Primary camera lookup:

```lua
local camera = Camera.findPrimary()
if camera.valid then
  camera.transform.position = Vec3(0, 6, 8)
  camera.transform:lookAt(self.transform.position)
end
```

The `Camera` reference (obtained via `entity:getComponent(Component.Camera)`)
exposes:

```lua
local cam = camera:getComponent(Component.Camera)
if cam then
  cam.primary = true
  cam.fovY = 60
end
```

- `camera.valid`
- `camera.primary`
- `camera.projection`
- `camera.fovY`
- `camera.orthographicHeight`
- `camera.cullingMask`
- `camera.rendererKey`

Layer mask constants are available through `Layer.Default`, `Layer.UI`, and
`Layer.All`.

## Input

Keyboard and mouse input are exposed through `Input`.

```lua
if Input.isKeyHeld(KeyCode.W) then
  print("W is held")
end

if Input.isKeyPressed(KeyCode.Space) then
  print("Space pressed this frame")
end
```

Keyboard functions:

- `Input.isKeyHeld(key)`
- `Input.isKeyPressed(key)`
- `Input.isKeyReleased(key)`
- `Input.isKeyRepeated(key)`

Mouse functions:

- `Input.isMouseButtonHeld(button)`
- `Input.isMouseButtonPressed(button)`
- `Input.isMouseButtonReleased(button)`
- `Input.mouseButtonClicks(button)`
- `Input.mousePosition()`
- `Input.mousePositionFlipY()`
- `Input.mousePositionDelta()`
- `Input.mouseScrollDelta()`

Use `KeyCode` and `MouseCode` enum values rather than integer literals.

### Gamepad

The first connected controller is exposed through `Input` (SDL_Gamepad, Xbox-style names).

```lua
if Input.isGamepadConnected() then
  local lx = Input.gamepadAxis(GamepadAxis.LeftX)      -- sticks: [-1, 1]
  local lt = Input.gamepadAxis(GamepadAxis.LeftTrigger) -- triggers: [0, 1]
  if Input.isGamepadButtonPressed(GamepadButton.South) then -- A / Cross
    Input.rumble(0.6, 0.6, 200) -- low, high in [0,1]; duration in ms
  end
end
```

- `Input.isGamepadConnected()`
- `Input.isGamepadButtonHeld(button)` / `Input.isGamepadButtonPressed(button)` / `Input.isGamepadButtonReleased(button)`
- `Input.gamepadAxis(axis)`
- `Input.rumble(lowFrequency, highFrequency, durationMs)`

Use `GamepadButton` (`South`/`East`/`West`/`North`, `Start`, `Back`, `Guide`,
`LeftShoulder`/`RightShoulder`, `LeftStick`/`RightStick`, `DpadUp`/`DpadDown`/`DpadLeft`/
`DpadRight`) and `GamepadAxis` (`LeftX`/`LeftY`/`RightX`/`RightY`, `LeftTrigger`/
`RightTrigger`) enum values.

### Input actions (project-level map)

Godot-style named actions let gameplay bind to intents ("Jump") instead of raw keys, and
players rebind. Actions are defined in the project file `res://input.actions.json`, loaded at
startup. Each action has a `deadzone` and a list of `events`; an action is active when any of
its events is active.

```json
{
  "actions": {
    "Jump":  { "deadzone": 0.5, "events": [ {"key":"Space"}, {"gamepadButton":"South"} ] },
    "MoveX": { "deadzone": 0.2, "events": [ {"gamepadAxis":"LeftX"}, {"key":"D","scale":1}, {"key":"A","scale":-1} ] }
  }
}
```

Event kinds: `key`, `mouseButton`, `gamepadButton`, `gamepadAxis`. The optional `scale`
sets the sign/magnitude of an event's analog contribution (e.g. `-1` for the "left" key of an
axis). Names use the stripped enum forms, matching the `KeyCode`/`GamepadButton`/`GamepadAxis`
tables (`Space`, `South`, `LeftX`).

```lua
if Input.isActionPressed("Jump") then jump() end   -- this-frame edge
local move = Input.actionAxis("MoveX")              -- signed [-1, 1], deadzone applied
```

- `Input.hasAction(name)`
- `Input.isActionHeld(name)` (currently down) / `Input.isActionPressed(name)` /
  `Input.isActionReleased(name)` (this-frame edges, like `isKeyHeld` vs `isKeyPressed`)
- `Input.actionAxis(name)` (combined signed analog value)

Runtime rebinding (in-memory; persist by editing the JSON):

- `Input.clearActionEvents(name)`
- `Input.bindActionKey(name, key)` / `Input.bindActionMouseButton(name, button)`
- `Input.bindActionGamepadButton(name, button)` / `Input.bindActionGamepadAxis(name, axis, scale)`

Complete example — attach to an entity; with the action map above it moves on WASD or the
left stick, jumps on Space / the South button (with rumble), and reads raw gamepad input:

```lua
function OnCreate(self)
  print("gamepad connected = " .. tostring(Input.isGamepadConnected()))
  -- Runtime rebind: also let the North/Y face button trigger Jump.
  Input.bindActionGamepadButton("Jump", GamepadButton.North)
end

function OnUpdate(self, dt)
  if Input.isActionPressed("Jump") then
    Input.rumble(0.6, 0.6, 200) -- short rumble burst
  end

  -- Analog movement combining stick + WASD.
  local moveX = Input.actionAxis("MoveX")
  local moveY = Input.actionAxis("MoveY")
  if math.abs(moveX) > 0.0 or math.abs(moveY) > 0.0 then
    local t = self.transform
    local pos = t.position
    pos.x = pos.x + moveX * dt * 4.0
    pos.z = pos.z + moveY * dt * 4.0
    t.position = pos
  end

  -- Raw device layer.
  if Input.isGamepadButtonPressed(GamepadButton.Start) then
    print("left trigger = " .. string.format("%.2f", Input.gamepadAxis(GamepadAxis.LeftTrigger)))
  end
end
```

### Touch

Multi-touch is exposed through `Input` (positions in window pixels). SDL also synthesizes mouse
events from the primary touch, so existing pointer UI works on touch automatically — these
queries add multi-touch / gestures.

```lua
for i = 0, Input.touchCount() - 1 do
  local p = Input.touchPosition(i)
  if Input.isTouchPressed(i) then print("touch began at", p.x, p.y) end
end
```

- `Input.touchCount()`
- `Input.touchId(i)` — stable id while the finger is down
- `Input.touchPosition(i)` / `Input.touchDelta(i)` (window pixels)
- `Input.isTouchPressed(i)` (began this frame) / `Input.isTouchReleased(i)` (ended this frame)

### Text input

For typed text (e.g. a chat box or custom field), enable text input while focused and read the
characters committed each frame:

- `Input.startTextInput()` / `Input.stopTextInput()`
- `Input.textInput()` — UTF-8 string typed this frame (`""` if none)

The built-in `UiInputField` widget does this for you; use these directly only for custom input.

## Physics

Rigid bodies are controlled with the `RigidBody` reference on an entity or the
global `Physics` table.

```lua
function OnFixedUpdate(self, fixedDt)
  local body = self:getComponent(Component.RigidBody)
  if not body then
    return
  end

  body:addForce(Vec3(10, 0, 0))
  body:activate()
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
  Character.move(self, Vec3(Input.axisX() * 4.0, 0, Input.axisZ() * 4.0))
  if Input.isKeyPressed("space") and Character.isGrounded(self) then
    Character.jump(self, 6.0)
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

### Constraints / joints

Connect two rigid bodies with a Jolt constraint (hinge, fixed, distance, slider,
point, cone). Pass `nil` as the second body to anchor to the world. `point`/`axis`
are world-space at creation; angles are degrees, distances meters. Each `add*`
returns an opaque constraint id (`0` on failure) for `removeConstraint` / motors.

```lua
-- A hinged door anchored to the world, with a motor that swings it open.
local hinge = Physics.addHingeConstraint(door, nil, Vec3(0, 1, -1), Vec3(0, 1, 0), -110, 0)
Physics.setConstraintMotor(hinge, true, 90, 200) -- target 90 deg/s, max torque 200

-- A rope: keep two bodies within [0.5, 4] meters.
local rope = Physics.addDistanceConstraint(bob, anchor, 0.5, 4.0)

-- Break a joint.
Physics.removeConstraint(rope)
```

- `Physics.addFixedConstraint(bodyA, bodyB?)`
- `Physics.addPointConstraint(bodyA, bodyB?, point)`
- `Physics.addDistanceConstraint(bodyA, bodyB?, minDistance, maxDistance)`
- `Physics.addHingeConstraint(bodyA, bodyB?, point, axis, minAngleDegrees, maxAngleDegrees)`
- `Physics.addSliderConstraint(bodyA, bodyB?, point, axis, minDistance, maxDistance)`
- `Physics.addConeConstraint(bodyA, bodyB?, point, twistAxis, halfAngleDegrees)`
- `Physics.removeConstraint(id)` / `Physics.isConstraintValid(id)`
- `Physics.setConstraintMotor(id, enabled, targetVelocity, maxForce)` — hinge motor is
  angular (deg/s), slider motor is linear (m/s)

Bodies are created on demand if the entity has a `RigidBodyComponent`. A constraint is
automatically removed when either referenced body is destroyed. A ragdoll is built from
capsule bodies linked by cone (swing-twist) constraints.

## Navigation (Recast/Detour)

Bake a navmesh from level geometry and steer agents along paths. Imported meshes with a
`MeshComponent` + `TransformComponent` are baked; builtin primitives are skipped.

```lua
function OnCreate(self)
  Nav.bake()                      -- build the navmesh from world geometry
  Nav.setDebugDrawEnabled(true)   -- visualize navmesh + paths in the editor
end

function OnUpdate(self, dt)
  if Input.isMouseButtonPressed(MouseCode.Left) then
    Nav.setAgentDestination(self, Vec3(10, 0, 4)) -- agent walks there, avoiding obstacles
  end
end
```

Add a `NavAgentComponent` (`entity:addComponent(Component.NavAgent)`) to make an entity an
agent; if it also has a `CharacterControllerComponent` the agent is steered through it,
otherwise its transform is moved directly.

- `Nav.bake()` — (re)build the navmesh; returns `true` on success
- `Nav.isBaked()`
- `Nav.findPath(start, end)` — returns an array of `Vec3` waypoints (empty if unreachable)
- `Nav.nearestPoint(point)` — closest point on the navmesh
- `Nav.setAgentDestination(entity, target)` / `Nav.stopAgent(entity)` / `Nav.agentHasPath(entity)`
- `Nav.setDebugDrawEnabled(enabled)` / `Nav.debugDrawEnabled()`

`NavAgent` fields: `radius`, `height`, `speed`, `stoppingDistance`, `targetPosition`,
`hasTarget`, `moving` (read-only).

Complete example — attach to an entity (ideally one with a `CharacterControllerComponent`).
It bakes the navmesh on start, makes itself an agent, draws the navmesh, and walks to a target
on left-click:

```lua
function OnCreate(self)
  if not self:hasComponent(Component.NavAgent) then
    self:addComponent(Component.NavAgent)
  end
  if not Nav.bake() then
    print("navmesh bake failed (no imported geometry?)")
  end
  Nav.setDebugDrawEnabled(true) -- show navmesh + paths in the editor
end

function OnUpdate(self, dt)
  if Input.isMouseButtonPressed(MouseCode.Left) then
    Nav.setAgentDestination(self, Vec3(4.0, 0.0, 0.0))
  end
  if Input.isKeyPressed(KeyCode.Escape) then
    Nav.stopAgent(self)
  end
  if Input.isKeyPressed(KeyCode.P) then
    local path = Nav.findPath(self.transform.position, Vec3(4.0, 0.0, 0.0))
    print("path has " .. #path .. " waypoints")
  end
end
```

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

  if Input.isKeyHeld(KeyCode.A) then x = x - 1.0 end
  if Input.isKeyHeld(KeyCode.D) then x = x + 1.0 end
  if Input.isKeyHeld(KeyCode.W) then z = z - 1.0 end
  if Input.isKeyHeld(KeyCode.S) then z = z + 1.0 end

  return Vec3(x, 0.0, z)
end

function OnCreate(self)
  RollABall.score = 0
  RollABall.won = false

  local body = self:getComponent(Component.RigidBody)
  if body then
    body.mass = 1.0
    body.overrideMass = true
    body.friction = 0.25
    body.restitution = 1.0
    body.gravityFactor = 1.0
  end

  print("[RollABall] started")
end

function OnFixedUpdate(self, fixedDt)
  local body = self:getComponent(Component.RigidBody)
  if not body then
    return
  end

  local input = movementInput()
  body:addForce(Vec3(input.x * RollABall.moveForce, 0.0, input.z * RollABall.moveForce))
  body:activate()
end

function OnUpdate(self, dt)
  local playerPos = self.transform.position

  local camera = Camera.findPrimary()
  if camera.valid then
    camera.transform.position = Vec3(playerPos.x, playerPos.y + 6.0, playerPos.z + 7.0)
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
- `Scene.dontDestroyOnLoad(entity)` — keep `entity` (and its subtree) across the next scene
  load / `Scene.instantiate(..., clearWorld=true)`; it is promoted to a root and survives
- `Scene.isPersistent(entity)`

## Save / Persistence

`Save` is a typed key/value store plus named, file-backed slots — for player progress,
options, unlocks, etc. The KV store lives in memory and **survives scene loads/reloads** (it
is an engine service, not world data); `Save.save(slot)` / `Save.load(slot)` persist it to and
restore it from disk (under the OS app-data dir, or `VULTRA_SAVE_DIR`).

```lua
-- Write progress, then persist slot 1.
Save.set("level", 3)
Save.set("playerName", "Ada")
Save.set("musicOn", true)
Save.save("slot1")

-- Later (even after a scene reload): restore and read back.
if Save.load("slot1") then
  local level = Save.get("level", 1)       -- 3 (default 1 if absent)
  local name  = Save.get("playerName", "?")
end
```

- `Save.set(key, value)` — `value` is a number, boolean, or string
- `Save.get(key, default?)` — returns the stored value (typed) or `default` / `nil`
- `Save.has(key)`, `Save.remove(key)`, `Save.clear()`
- `Save.save(slot)` / `Save.load(slot)` -> boolean
- `Save.hasSlot(slot)`, `Save.deleteSlot(slot)`, `Save.listSlots()` -> array of slot names

Slot names are sanitized to a safe filename. For complex tables, store JSON strings (encode
in Lua) under a key.

## Localization (I18n)

Translate game text at runtime through `I18n`, which resolves catalog keys against the active
language (the same catalogs the editor uses, plus any the game registers). A missing key
returns the key itself.

```lua
I18n.setLanguage("zh-CN")
label.text = I18n.tr("game.menu.start")            -- localized string
local hp = string.format(I18n.tr("game.hud.hp"), 80, 100) -- format args via string.format
```

- `I18n.tr(key)` -> string (key if missing)
- `I18n.setLanguage(locale)` -> boolean ; `I18n.language()` -> current locale
- `I18n.languages()` -> array of available locales ; `I18n.displayName(locale)` -> string
- `I18n.setPseudolocalize(enabled)` — bracket all strings to find un-translated literals

For authored UI, set a `UiText` component's **localizationKey** (in the inspector or
`uiText.localizationKey = "game.menu.start"`); the text then auto-resolves to the active
language each frame, no script needed.

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
- `Render.gaussianSplatSettings()`
- `Render.setGaussianSplatSettings(settings)`
- `Render.gaussianSplatFrameStats()`
- `RenderBackend.isXREnabled()`
- `RenderBackend.isXRMirrorEnabled()`
- `RenderBackend.isExitRequested()`

Upscaler APIs:

- `Upscaler.providers()`
- `Upscaler.active()`
- `Upscaler.setActive(name)`
- `Upscaler.setEnabled(enabled)`
- `Upscaler.setMode(mode)` where mode is `off`, `quality`, `balanced`, `performance`, `ultra_performance`, or `dlaa`
- `Upscaler.status()`

These are high-level controls only. Lua scripts cannot access native textures, command buffers, Vulkan
handles, or provider-owned SDK objects.

## Audio

Audio playback is driven through the global `Audio` table. Clips are addressed
by either a registry UUID string or a `res://` URI; pass the same string to any
clip-taking call. Sound-handle functions take the `SoundId` returned by the
`play*` calls. When no audio backend is available the calls are safe no-ops
(`backendReady()` reports `false`).

```lua
function OnCreate(self)
  Audio.preloadClip("res://audio/coin.wav")
end

function OnUpdate(self, dt)
  if Input.isKeyPressed(KeyCode.Space) then
    Audio.playOneShot("res://audio/coin.wav", 0.8) -- volume, optional pitch
  end
end
```

One-shots and music:

- `Audio.preloadClip(clip)` -- warm the cache; returns `true` on success
- `Audio.playOneShot(clip, volume?, pitch?)` -- fire-and-forget 2D sound, returns a `SoundId`
- `Audio.playOneShotAt(clip, position, volume?, pitch?)` -- spatialized one-shot at a world position
- `Audio.playMusic(clip, options?)` -- looping by default; `options` is a table of `volume`, `pitch`, `loop`, `fadeInMs`
- `Audio.stopMusic(fadeOutMs?)`

Sound-handle control (by `SoundId`):

- `Audio.stopSound(id, fadeOutMs?)`
- `Audio.pauseSound(id)` / `Audio.resumeSound(id)`
- `Audio.setVolume(id, volume)` / `Audio.setPitch(id, pitch)` / `Audio.setLooping(id, loop)`
- `Audio.isPlaying(id)`

Entity-driven playback (uses the entity's `AudioSourceComponent`):

- `Audio.play(entity, restart?)`
- `Audio.pause(entity)`
- `Audio.stop(entity)`

Global mixer:

- `Audio.setMasterVolume(volume)` / `Audio.masterVolume()`
- `Audio.backendReady()`

Components are reachable through `entity:getComponent(Component.AudioSource)` and
`entity:getComponent(Component.AudioListener)`:

```lua
local source = self:getComponent(Component.AudioSource)
if source then
  source.clip = "res://audio/engine.wav"
  source.volume = 0.5
  source.loop = true
  source.spatial = true
  Audio.play(self)
end
```

`AudioSource` properties: `valid`, `clip`, `volume`, `pitch`, `loop`,
`playOnStart`, `playing`, `spatial`, `minDistance`, `maxDistance`, `rolloff`.
`AudioListener` properties: `valid`, `primary`.

## Animation

Animator playback can be controlled either through the `Animator` component
reference (`entity:getComponent(Component.Animator)`) or the global `Animation`
table.

```lua
local actor = World.findByName("Actor")
if actor.valid then
  local animator = actor:getComponent(Component.Animator)
  if animator then
    animator:play(true)
    animator.speed = 1.0
    animator.loop = true

    local state = animator:state()
    print("animation time " .. tostring(state.time))
  end
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

Animation-controller parameters (for entities with controller data):

- `Animation.setFloat(entity, name, value)` / `Animation.getFloat(entity, name)`
- `Animation.setBool(entity, name, value)` / `Animation.getBool(entity, name)`
- `Animation.setTrigger(entity, name)`
- `Animation.currentState(entity)` -- returns `{valid, currentState, nextState,
  transitioning, transitionProgress, normalizedTime}`

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

### Keyframe events

Animator-graph states can carry **events** — named markers at a normalized time in the
clip. When playback crosses an event, the animation system calls the entity script's
`OnAnimationEvent(self, name)` (see **Animation Event Callback**). Author them per state in
the `.vanimgraph.json`:

```json
{
  "states": [
    {
      "name": "Walk",
      "animation": "<clip-uuid>",
      "loop": true,
      "events": [
        { "name": "footstepLeft",  "normalizedTime": 0.25 },
        { "name": "footstepRight", "normalizedTime": 0.75 }
      ],
      "transitions": []
    }
  ],
  "version": 1
}
```

`normalizedTime` is `[0,1]` within the clip. Events fire on the active state, repeat each
loop, and are scoped to that state across transitions. (Single-clip animators — `mode 0` —
have no states, so events require the graph mode.)

### Blend trees

A graph state can be a **1D blend tree** instead of a single clip: it blends several clips by a
float parameter (e.g. idle/walk/run by `speed`). Author it on the state in `.vanimgraph.json`;
clips are sampled at a shared, phase-matched ratio and blended by the two entries bracketing
the parameter value. Drive the parameter with `Animation.setFloat(entity, "speed", v)`.

```json
{
  "states": [
    {
      "name": "Locomotion",
      "loop": true,
      "blendTree": {
        "parameter": "speed",
        "entries": [
          { "animation": "<idle-uuid>", "threshold": 0.0 },
          { "animation": "<walk-uuid>", "threshold": 1.0 },
          { "animation": "<run-uuid>",  "threshold": 4.0 }
        ]
      }
    }
  ],
  "version": 1
}
```

When `blendTree.entries` is present it overrides the state's single `animation`. Events still
fire on a blend-tree state.

### Root motion

Set `AnimatorComponent.applyRootMotion = true` (graph mode) to let the playing clip's root
joint drive the **entity transform** (horizontal motion) instead of sliding the mesh in place.
The displayed skeleton stays centered on the entity; the transform absorbs the per-frame root
delta (rotated by the entity's orientation). It is skipped during cross-fades and on the
loop-wrap frame, and assumes joint 0 is the skeleton root.

## Common Pitfalls

- Use `:` for bound methods such as `entity:destroy()` and
  `entity:getComponent(Component.RigidBody):addForce(force)`.
- Check `entity.valid` and component-specific `valid` fields before using
  handles found by name.
- Reach every component other than `transform` through the generic
  `entity:getComponent(Component.X)` (returns `nil` when absent),
  `entity:addComponent(Component.X)`, `entity:removeComponent(Component.X)`, and
  `entity:hasComponent(Component.X)`. The old per-component properties
  (`entity.rigidBody`, `entity.camera`, ...), the `entity:hasRigidBody()`-style
  methods, and the `World.add*/remove*` family were removed; bind the new value
  to a local and nil-check it before use.
- Use `OnFixedUpdate` for physics forces. Use `OnUpdate` for input sampling,
  camera follow, score checks, and non-physics animation.
- Prefer the `OnCollisionEnter/Stay/Exit` and `OnTriggerEnter/Stay/Exit`
  callbacks (see "Collision And Trigger Callbacks") for contact-driven logic.
  For polling-style queries, `Physics.overlapSphere`, `Physics.overlapBox`,
  `Physics.contactPairs`, and `Physics.contactEvents` are also available.
- Beyond single-clip playback, the `Animation` table also drives
  animation-controller state machines via parameters: `Animation.setFloat`,
  `Animation.setBool`, `Animation.setTrigger`, `Animation.getFloat`,
  `Animation.getBool`, and `Animation.currentState(entity)` (returns the current
  controller state, transition progress, and normalized time). These require the
  entity to have controller data authored; masks and retargeting are not yet
  exposed.
- Imported visual assets may have very small or very large source units. Verify
  scale in Game View after instantiation.
- Embedded textures inside GLB imports must be validated by rendered material
  output, not only by registry/import success.
