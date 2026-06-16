---@meta
-- Vultra Lua scripting API stubs (LuaLS annotations).
-- Hand-maintained for now; kept in sync with the C++ bindings by
-- tests/lua_api_conformance (stale entries here are a hard test failure,
-- missing coverage is tracked in its exceptions.lua burn-down list).
-- See doc/lua_api_design.md for the API rules.

--- 2D vector
---@class Vec2
---@operator add(Vec2): Vec2
---@operator sub(Vec2): Vec2
---@operator unm(Vec2): Vec2
---@operator mul(number): Vec2
---@operator div(number): Vec2
---@field x number
---@field y number
local Vec2 = {}
---@return Vec2
---@overload fun(x: number, y: number): Vec2
function Vec2() end

--- 3D vector
---@class Vec3
---@operator add(Vec3): Vec3
---@operator sub(Vec3): Vec3
---@operator unm(Vec3): Vec3
---@operator mul(number): Vec3
---@operator div(number): Vec3
---@field x number
---@field y number
---@field z number
local Vec3 = {}
---@return Vec3
---@overload fun(x: number, y: number, z: number): Vec3
function Vec3() end

--- 4D vector
---@class Vec4
---@operator add(Vec4): Vec4
---@operator sub(Vec4): Vec4
---@operator unm(Vec4): Vec4
---@operator mul(number): Vec4
---@operator div(number): Vec4
---@field x number
---@field y number
---@field z number
---@field w number
local Vec4 = {}
---@return Vec4
---@overload fun(x: number, y: number, z: number, w: number): Vec4
function Vec4() end

--- Construct a 2D vector
---@deprecated Use Vec2(x, y)
---@param x number
---@param y number
---@return Vec2
function vec2(x, y) end

--- Construct a 3D vector
---@deprecated Use Vec3(x, y, z)
---@param x number
---@param y number
---@param z number
---@return Vec3
function vec3(x, y, z) end

--- Construct a 4D vector
---@deprecated Use Vec4(x, y, z, w)
---@param x number
---@param y number
---@param z number
---@param w number
---@return Vec4
function vec4(x, y, z, w) end

--- Dot product
---@param a Vec2
---@param b Vec2
---@return number
---@overload fun(a: Vec3, b: Vec3): number
---@overload fun(a: Vec4, b: Vec4): number
function dot(a, b) end

--- Squared length of a vector
---@param v Vec2
---@return number
---@overload fun(v: Vec3): number
---@overload fun(v: Vec4): number
function lengthSquared(v) end

--- Start a coroutine owned by this entity script. The first slice runs
--- immediately; later slices resume on the frame tick. Coroutines stop on
--- OnDisable/OnDestroy. Only available inside entity scripts.
---@param fn fun(...)
---@param ... any @ passed to fn
---@return thread
function startCoroutine(fn, ...) end

--- Stop all coroutines owned by this entity script.
function stopAllCoroutines() end

--- Suspend the current coroutine for a duration (seconds).
---@param seconds number
function wait(seconds) end

--- Suspend the current coroutine for a number of frames.
---@param frames integer
function waitFrames(frames) end

--- Easing functions: map a normalized t in [0,1] to an eased value in [0,1].
--- Pass one as `opts.ease` to Tween.to (e.g. Ease.cubicInOut).
---@class Ease
---@field linear fun(t:number):number
---@field quadIn fun(t:number):number
---@field quadOut fun(t:number):number
---@field quadInOut fun(t:number):number
---@field cubicIn fun(t:number):number
---@field cubicOut fun(t:number):number
---@field cubicInOut fun(t:number):number
---@field sineIn fun(t:number):number
---@field sineOut fun(t:number):number
---@field sineInOut fun(t:number):number
---@field expoIn fun(t:number):number
---@field expoOut fun(t:number):number
---@field backOut fun(t:number):number
---@field bounceOut fun(t:number):number
Ease = {}

--- Frame-driven tweening of a table's numeric fields.
---@class Tween
Tween = {}

--- Interpolate `obj`'s fields toward `toFields` over `duration` seconds.
--- `opts` is an optional table: { ease = fun(t):number, onUpdate = fun(t), onComplete = fun() }.
---@param obj table
---@param toFields table @ { field = targetNumber, ... }
---@param duration number @ seconds
---@param opts? table
---@return integer handle @ pass to Tween.cancel
function Tween.to(obj, toFields, duration, opts) end

--- Cancel a running tween.
---@param handle integer
function Tween.cancel(handle) end

--- Delayed and repeating callbacks driven by the frame tick.
---@class Timer
Timer = {}

--- Call `fn` once after `seconds`.
---@param seconds number
---@param fn fun()
---@return integer handle
function Timer.after(seconds, fn) end

--- Call `fn` every `seconds`; returning false from `fn` stops the timer.
---@param seconds number
---@param fn fun():boolean?
---@return integer handle
function Timer.every(seconds, fn) end

--- Cancel a running timer.
---@param handle integer
function Timer.cancel(handle) end

--- An ordered sequence of waits and calls. Chain `:wait`/`:call`, then `:start`.
---@class Timeline
Timeline = {}

--- Create a new timeline.
---@return Timeline
function Timeline.new() end

--- Append a wait step (seconds). Returns self for chaining.
---@param seconds number
---@return Timeline
function Timeline:wait(seconds) end

--- Append a call step. Returns self for chaining.
---@param fn fun()
---@return Timeline
function Timeline:call(fn) end

--- Start running the timeline.
---@return integer handle
function Timeline:start() end

--- Keyboard and mouse polling. Boolean queries are isX; value queries are nouns.
---@class Input
Input = {}

--- True while the key is held down.
---@param key integer @ a KeyCode value
---@return boolean
function Input.isKeyHeld(key) end

--- True on the frame the key went down.
---@param key integer @ a KeyCode value
---@return boolean
function Input.isKeyPressed(key) end

--- True on the frame the key was released.
---@param key integer @ a KeyCode value
---@return boolean
function Input.isKeyReleased(key) end

--- True on OS key-repeat frames while the key is held.
---@param key integer @ a KeyCode value
---@return boolean
function Input.isKeyRepeated(key) end

--- True while the mouse button is held down.
---@param button integer @ a MouseCode value
---@return boolean
function Input.isMouseButtonHeld(button) end

--- True on the frame the mouse button went down.
---@param button integer @ a MouseCode value
---@return boolean
function Input.isMouseButtonPressed(button) end

--- True on the frame the mouse button was released.
---@param button integer @ a MouseCode value
---@return boolean
function Input.isMouseButtonReleased(button) end

--- Click count for the button this frame (2 = double click).
---@param button integer @ a MouseCode value
---@return integer
function Input.mouseButtonClicks(button) end

--- Mouse position in window pixels (origin top-left).
---@return Vec2
function Input.mousePosition() end

--- Mouse position in window pixels (origin bottom-left).
---@return Vec2
function Input.mousePositionFlipY() end

--- Mouse movement since last frame, in pixels.
---@return Vec2
function Input.mousePositionDelta() end

--- Scroll wheel delta this frame.
---@return Vec2
function Input.mouseScrollDelta() end

--- Entity handle. Check entity.valid before use across frames.
--- Only the always-on members are direct fields (valid/id/name/active/visible/
--- transform, declared on the generated Entity class below); every other
--- component is reached through addComponent/getComponent/removeComponent/
--- hasComponent, keyed by a Component.* token.
---@class Entity
local Entity = {}

--- Add a component (no-op if already present) and return its reference.
---@param type integer @ a Component.* token (e.g. Component.RigidBody)
---@return any @ the component reference
function Entity:addComponent(type) end

--- Get a component reference, or nil if the entity does not have it.
---@param type integer @ a Component.* token
---@return any|nil
function Entity:getComponent(type) end

--- Remove a component. Returns true if one was removed.
---@param type integer @ a Component.* token
---@return boolean
function Entity:removeComponent(type) end

--- Whether the entity has the given component.
---@param type integer @ a Component.* token
---@return boolean
function Entity:hasComponent(type) end

--- Convenience constructors that create an entity with common components already
--- attached. Each returns the new entity handle.
---@class WorldHelper
WorldHelper = {}

--- Create an empty entity (transform only).
---@param name? string
---@return Entity
function WorldHelper.addEmpty(name) end

--- Create an entity with a primary CameraComponent.
---@param name? string
---@return Entity
function WorldHelper.addMainCamera(name) end

--- Create an entity with a CameraComponent.
---@param name? string
---@return Entity
function WorldHelper.addCamera(name) end

--- Create an entity with a LightComponent.
---@param name? string
---@return Entity
function WorldHelper.addLight(name) end

--- Create an entity with a MeshComponent.
---@param name? string
---@return Entity
function WorldHelper.addMesh(name) end

-- imgui-ext extension tables. Like ImGui, these keep upstream PascalCase names
-- and are only present when an ImGui service is active (editor/dev builds).
-- Matrices are arrays of 16 numbers (column-major); vec3s are arrays of 3.

--- ImGuizmo: 3D transform gizmos. Call inside an ImGui window.
---@class ImGuizmo
---@field OPERATION table<string, integer> @ TRANSLATE/ROTATE/SCALE/SCALEU/UNIVERSAL/BOUNDS + per-axis
---@field MODE table<string, integer> @ LOCAL / WORLD
ImGuizmo = {}
function ImGuizmo.BeginFrame() end
---@param enable boolean
function ImGuizmo.Enable(enable) end
---@param ortho boolean
function ImGuizmo.SetOrthographic(ortho) end
function ImGuizmo.SetDrawlist() end
---@param x number @param y number @param width number @param height number
function ImGuizmo.SetRect(x, y, width, height) end
---@return boolean
function ImGuizmo.IsOver() end
---@return boolean
function ImGuizmo.IsUsing() end
---@return boolean
function ImGuizmo.IsUsingAny() end
--- Manipulate a model matrix. Returns changed, newMatrix.
---@param view number[] @param projection number[] @param operation integer @param mode integer @param matrix number[] @param snap? number[]
---@return boolean, number[]
function ImGuizmo.Manipulate(view, projection, operation, mode, matrix, snap) end
---@param matrix number[]
---@return number[], number[], number[] @ translation, rotation(deg), scale
function ImGuizmo.DecomposeMatrixToComponents(matrix) end
---@param translation number[] @param rotation number[] @param scale number[]
---@return number[]
function ImGuizmo.RecomposeMatrixFromComponents(translation, rotation, scale) end

--- ImOGuizmo: orientation cube. Returns interacted, newView from DrawGizmo.
---@class ImOGuizmo
ImOGuizmo = {}
---@param x number @param y number @param size number
function ImOGuizmo.SetRect(x, y, size) end
function ImOGuizmo.SetDrawList() end
---@param background? boolean
function ImOGuizmo.BeginFrame(background) end
---@param view number[] @param projection number[] @param pivotDistance? number
---@return boolean, number[]
function ImOGuizmo.DrawGizmo(view, projection, pivotDistance) end

--- ImPlot: plotting. Wrap plots in BeginPlot/EndPlot; data are number arrays.
---@class ImPlot
---@field Axis table<string, integer> @ X1/X2/X3/Y1/Y2/Y3
---@field Flags table<string, integer>
---@field AxisFlags table<string, integer>
---@field Location table<string, integer> @ legend location
ImPlot = {}
---@param title string @param sizeX? number @param sizeY? number @param flags? integer
---@return boolean
function ImPlot.BeginPlot(title, sizeX, sizeY, flags) end
function ImPlot.EndPlot() end
---@param xLabel string @param yLabel string @param xFlags? integer @param yFlags? integer
function ImPlot.SetupAxes(xLabel, yLabel, xFlags, yFlags) end
---@param xMin number @param xMax number @param yMin number @param yMax number @param cond? integer
function ImPlot.SetupAxesLimits(xMin, xMax, yMin, yMax, cond) end
---@param location integer @param flags? integer
function ImPlot.SetupLegend(location, flags) end
--- Plot a line. Plot*(label, ys) uses implicit x; Plot*(label, xs, ys) is explicit.
---@param label string @param a number[] @param b? number[]
function ImPlot.PlotLine(label, a, b) end
---@param label string @param a number[] @param b? number[]
function ImPlot.PlotScatter(label, a, b) end
---@param label string @param a number[] @param b? number[] @param barSize? number
function ImPlot.PlotBars(label, a, b, barSize) end

--- ImGuiFileDialog: modal file/folder picker.
---@class ImGuiFileDialog
ImGuiFileDialog = {}
---@param key string @param title string @param filters? string @param path? string
function ImGuiFileDialog.OpenDialog(key, title, filters, path) end
---@param key string @param flags? integer
---@return boolean @ true the frame a result is produced
function ImGuiFileDialog.Display(key, flags) end
---@return boolean
function ImGuiFileDialog.IsOk() end
---@return string
function ImGuiFileDialog.GetFilePathName() end
---@return string
function ImGuiFileDialog.GetCurrentFileName() end
---@return string
function ImGuiFileDialog.GetCurrentPath() end
---@return table<string, string> @ fileName -> filePathName
function ImGuiFileDialog.GetSelection() end
function ImGuiFileDialog.Close() end
---@param key? string
---@return boolean
function ImGuiFileDialog.IsOpened(key) end

--- ImNodes: node-graph editor. Wrap in BeginNodeEditor/EndNodeEditor.
---@class ImNodes
---@field PinShape table<string, integer> @ Circle/CircleFilled/Triangle/Quad
ImNodes = {}
function ImNodes.BeginNodeEditor() end
function ImNodes.EndNodeEditor() end
---@param id integer
function ImNodes.BeginNode(id) end
function ImNodes.EndNode() end
function ImNodes.BeginNodeTitleBar() end
function ImNodes.EndNodeTitleBar() end
---@param id integer
function ImNodes.BeginInputAttribute(id) end
function ImNodes.EndInputAttribute() end
---@param id integer
function ImNodes.BeginOutputAttribute(id) end
function ImNodes.EndOutputAttribute() end
---@param id integer
function ImNodes.BeginStaticAttribute(id) end
function ImNodes.EndStaticAttribute() end
---@param id integer @param startAttr integer @param endAttr integer
function ImNodes.Link(id, startAttr, endAttr) end
---@param id integer @param x number @param y number
function ImNodes.SetNodeGridSpacePos(id, x, y) end
---@param id integer @param x number @param y number
function ImNodes.SetNodeScreenSpacePos(id, x, y) end
---@return boolean, integer, integer @ created, startAttr, endAttr
function ImNodes.IsLinkCreated() end
---@return boolean, integer @ destroyed, linkId
function ImNodes.IsLinkDestroyed() end
---@return boolean, integer @ hovered, nodeId
function ImNodes.IsNodeHovered() end

--- Gaussian splat renderer settings snapshot (see Render.gaussianSplatSettings).
--- TODO(stub): document fields.
---@class GaussianSplatSettings
local GaussianSplatSettings = {}

--- Per-frame Gaussian splat statistics (see Render.gaussianSplatFrameStats).
--- TODO(stub): document fields.
---@class GaussianSplatFrameStats
local GaussianSplatFrameStats = {}

--- Entity transform reference (entity.transform). Angles are euler degrees.
---@class Transform
---@field position Vec3
---@field scale Vec3
---@field rotation Vec3 @ euler degrees
local Transform = {}

--- Move by a delta in world units (UI: pixels).
---@param delta Vec3
function Transform:translate(delta) end

--- Rotate to face a world-space target.
---@param target Vec3
function Transform:lookAt(target) end

--- Set rotation from euler degrees.
---@deprecated Assign transform.rotation instead
---@param value Vec3
function Transform:setEulerDegrees(value) end

--- UI layout reference (entity.rectTransform). Px-suffixed values are pixels.
---@class RectTransform
---@field anchorMin Vec2
---@field anchorMax Vec2
---@field pivot Vec2
---@field anchoredPositionPx Vec2
---@field sizeDeltaPx Vec2
---@field scale Vec2
---@field rotation number @ Z rotation in degrees
local RectTransform = {}

--- Button component reference (entity.uiButton).
---@class UiButton
---@field interactable boolean
---@field hovered boolean @ read-only
---@field pressed boolean @ read-only
---@field clickedThisFrame boolean @ read-only
---@field onClick UiSignal
local UiButton = {}

--- Toggle component reference (entity.uiToggle).
---@class UiToggle
---@field interactable boolean
---@field checked boolean
---@field onClick UiSignal
local UiToggle = {}

--- Single-line editable text field reference (entity.uiInputField).
---@class UiInputField
---@field text string
---@field placeholder string
---@field interactable boolean
---@field focused boolean @ read-only
---@field submitted boolean @ read-only
---@field onValueChanged UiSignal
---@field onSubmit UiSignal
local UiInputField = {}

--- Dropdown selector reference (entity.uiDropdown).
---@class UiDropdown
---@field selectedIndex integer @ 0-based index of the selected option
---@field options string[]
---@field interactable boolean
---@field expanded boolean @ read-only
---@field onValueChanged UiSignal
local UiDropdown = {}

--- Multi-subscriber UI signal; connect returns a UiSignalConnection.
---@class UiSignal
local UiSignal = {}

---@param callback fun(event: table)
---@return UiSignalConnection
function UiSignal:connect(callback) end

---@class UiSignalConnection
local UiSignalConnection = {}

function UiSignalConnection:disconnect() end

--- Animation playback and animator-graph parameters.
---@class Animation
Animation = {}

--- Read an animator-graph float parameter (symmetric with setFloat).
---@param entity Entity
---@param name string
---@return number
function Animation.getFloat(entity, name) end

--- Write an animator-graph float parameter.
---@param entity Entity
---@param name string
---@param value number
---@return boolean
function Animation.setFloat(entity, name, value) end

--- Read an animator-graph bool parameter (symmetric with setBool).
---@param entity Entity
---@param name string
---@return boolean
function Animation.getBool(entity, name) end

--- Write an animator-graph bool parameter.
---@param entity Entity
---@param name string
---@param value boolean
---@return boolean
function Animation.setBool(entity, name, value) end

--- Fire an animator-graph trigger parameter (auto-resets).
---@param entity Entity
---@param name string
---@return boolean
function Animation.setTrigger(entity, name) end

--- Rendering control.
---@class Render
Render = {}

--- Current Gaussian splat settings snapshot.
---@return GaussianSplatSettings
function Render.gaussianSplatSettings() end

--- Apply Gaussian splat settings.
---@param settings GaussianSplatSettings
function Render.setGaussianSplatSettings(settings) end

--- Per-frame Gaussian splat statistics.
---@return GaussianSplatFrameStats
function Render.gaussianSplatFrameStats() end

-- <<<BEGIN GENERATED BINDINGS (gen_lua_bindings.py) -- do not edit>>>

--- Component reference generated from AudioListenerComponent.
---@class AudioListener
---@field valid boolean @ read-only
---@field primary boolean
local AudioListener = {}

--- Component reference generated from AudioSourceComponent.
---@class AudioSource
---@field valid boolean @ read-only
---@field clip string
---@field volume number
---@field pitch number
---@field loop boolean
---@field playOnStart boolean
---@field playing boolean
---@field spatial boolean
---@field minDistance number
---@field maxDistance number
---@field rolloff number
local AudioSource = {}

--- Component reference generated from BoxShapeComponent.
---@class BoxShape
---@field valid boolean @ read-only
---@field halfExtents Vec3
local BoxShape = {}

--- Component reference generated from CameraComponent.
---@class CameraRef
---@field valid boolean @ read-only
---@field primary boolean
---@field projection integer
---@field fovY number
---@field fovYDegrees number @ deprecated, use fovY
---@field orthographicHeight number
---@field zNear number
---@field zFar number
---@field clearMode integer
---@field clearColor Vec4
---@field priority integer
---@field cullingMask integer
---@field rendererKey string
local CameraRef = {}

--- Component reference generated from CapsuleShapeComponent.
---@class CapsuleShape
---@field valid boolean @ read-only
---@field halfHeightOfCylinder number
---@field radius number
local CapsuleShape = {}

--- Component reference generated from CylinderShapeComponent.
---@class CylinderShape
---@field valid boolean @ read-only
---@field halfHeight number
---@field radius number
local CylinderShape = {}

--- Component reference generated from EnvironmentComponent.
---@class Environment
---@field valid boolean @ read-only
---@field active boolean
---@field skybox string
---@field ambientColor Vec3
---@field ambientIntensity number
---@field enableIBL boolean
---@field iblColor Vec3
---@field iblIntensity number
local Environment = {}

--- Component reference generated from LightComponent.
---@class Light
---@field valid boolean @ read-only
---@field kind integer
---@field color Vec3
---@field intensity number
---@field range number
---@field radius number
---@field width number
---@field height number
---@field castsShadow boolean
---@field twoSided boolean
local Light = {}

--- Component reference generated from ParticleEmitterComponent.
---@class ParticleEmitter
---@field valid boolean @ read-only
---@field playing boolean
---@field worldSpace boolean
---@field gpu boolean
---@field maxParticles integer
---@field emissionRate number
---@field lifetime number
---@field lifetimeVariance number
---@field spawnRadius number
---@field startVelocity Vec3
---@field velocityVariance number
---@field gravity Vec3
---@field startSize number
---@field endSize number
---@field startColor Vec4
---@field endColor Vec4
local ParticleEmitter = {}

--- Component reference generated from ReflectionProbeComponent.
---@class ReflectionProbe
---@field valid boolean @ read-only
---@field active boolean
---@field enableIBL boolean
---@field environmentMap string
---@field shape integer
---@field boxSize Vec3
---@field radius number
---@field blendDistance number
---@field intensity number
---@field priority integer
---@field parallaxCorrection boolean
local ReflectionProbe = {}

--- Component reference generated from SphereShapeComponent.
---@class SphereShape
---@field valid boolean @ read-only
---@field radius number
local SphereShape = {}

-- <<<END GENERATED BINDINGS>>>

-- <<<BEGIN GENERATED IMGUI BINDINGS (gen_imgui_lua.py) -- do not edit>>>

--- Dear ImGui subset (editor/dev builds only; absent in headless runtimes).
--- Calls outside the ImGui frame raise an error. Upstream PascalCase names.
---@class ImGui
---@field WindowFlags table<string, integer>
---@field ChildFlags table<string, integer>
---@field Cond table<string, integer>
---@field Col table<string, integer>
---@field StyleVar table<string, integer>
---@field TableFlags table<string, integer>
---@field TableColumnFlags table<string, integer>
---@field TableRowFlags table<string, integer>
---@field SelectableFlags table<string, integer>
---@field ComboFlags table<string, integer>
---@field TreeNodeFlags table<string, integer>
---@field PopupFlags table<string, integer>
---@field HoveredFlags table<string, integer>
---@field InputTextFlags table<string, integer>
---@field SliderFlags table<string, integer>
---@field TabBarFlags table<string, integer>
---@field TabItemFlags table<string, integer>
ImGui = {}

---@param name string
---@param p_open? boolean
---@param flags? integer
---@return boolean
---@return boolean
function ImGui.Begin(name, p_open, flags) end

function ImGui.End() end

---@param str_id string
---@param size? Vec2
---@param child_flags? integer
---@param window_flags? integer
---@return boolean
function ImGui.BeginChild(str_id, size, child_flags, window_flags) end

function ImGui.EndChild() end

---@return number
function ImGui.GetWindowWidth() end

---@return number
function ImGui.GetWindowHeight() end

---@return Vec2
function ImGui.GetContentRegionAvail() end

---@param pos Vec2
---@param cond? integer
function ImGui.SetNextWindowPos(pos, cond) end

---@param size Vec2
---@param cond? integer
function ImGui.SetNextWindowSize(size, cond) end

function ImGui.Separator() end

---@param label string
function ImGui.SeparatorText(label) end

function ImGui.SameLine() end

function ImGui.Spacing() end

function ImGui.NewLine() end

function ImGui.Indent() end

function ImGui.Unindent() end

---@param size Vec2
function ImGui.Dummy(size) end

---@param text string
function ImGui.Text(text) end

---@param col Vec4
---@param text string
function ImGui.TextColored(col, text) end

---@param text string
function ImGui.TextDisabled(text) end

---@param text string
function ImGui.TextWrapped(text) end

---@param text string
function ImGui.BulletText(text) end

---@param label string
---@return boolean
function ImGui.Button(label) end

---@param label string
---@return boolean
function ImGui.SmallButton(label) end

---@param label string
---@param v boolean
---@return boolean
---@return boolean
function ImGui.Checkbox(label, v) end

---@param label string
---@param active boolean
---@return boolean
function ImGui.RadioButton(label, active) end

---@param fraction number
---@param size_arg? Vec2
---@param overlay? string
function ImGui.ProgressBar(fraction, size_arg, overlay) end

function ImGui.Bullet() end

---@param label string
---@param v number
---@param v_min number
---@param v_max number
---@return boolean
---@return number
function ImGui.SliderFloat(label, v, v_min, v_max) end

---@param label string
---@param v integer
---@param v_min integer
---@param v_max integer
---@return boolean
---@return integer
function ImGui.SliderInt(label, v, v_min, v_max) end

---@param label string
---@param v number
---@return boolean
---@return number
function ImGui.DragFloat(label, v) end

---@param label string
---@param v integer
---@return boolean
---@return integer
function ImGui.DragInt(label, v) end

---@param label string
---@param v number
---@return boolean
---@return number
function ImGui.InputFloat(label, v) end

---@param label string
---@param v integer
---@return boolean
---@return integer
function ImGui.InputInt(label, v) end

---@param label string
---@param preview_value string
---@param flags? integer
---@return boolean
function ImGui.BeginCombo(label, preview_value, flags) end

function ImGui.EndCombo() end

---@param label string
---@return boolean
function ImGui.Selectable(label) end

---@param label string
---@return boolean
function ImGui.TreeNode(label) end

function ImGui.TreePop() end

---@param label string
---@param flags? integer
---@return boolean
function ImGui.CollapsingHeader(label, flags) end

---@param is_open boolean
---@param cond? integer
function ImGui.SetNextItemOpen(is_open, cond) end

---@return boolean
function ImGui.BeginMenuBar() end

function ImGui.EndMenuBar() end

---@return boolean
function ImGui.BeginMainMenuBar() end

function ImGui.EndMainMenuBar() end

---@param label string
---@return boolean
function ImGui.BeginMenu(label) end

function ImGui.EndMenu() end

---@param label string
---@return boolean
function ImGui.MenuItem(label) end

---@param str_id string
---@param popup_flags? integer
function ImGui.OpenPopup(str_id, popup_flags) end

---@param str_id string
---@param flags? integer
---@return boolean
function ImGui.BeginPopup(str_id, flags) end

function ImGui.EndPopup() end

---@param name string
---@param p_open? boolean
---@param flags? integer
---@return boolean
---@return boolean
function ImGui.BeginPopupModal(name, p_open, flags) end

function ImGui.CloseCurrentPopup() end

---@return boolean
function ImGui.BeginTooltip() end

function ImGui.EndTooltip() end

---@param text string
function ImGui.SetTooltip(text) end

---@param text string
function ImGui.SetItemTooltip(text) end

---@param str_id string
---@param flags? integer
---@return boolean
function ImGui.BeginTabBar(str_id, flags) end

function ImGui.EndTabBar() end

---@param label string
---@param p_open? boolean
---@param flags? integer
---@return boolean
---@return boolean
function ImGui.BeginTabItem(label, p_open, flags) end

function ImGui.EndTabItem() end

---@param str_id string
---@param columns integer
---@param flags? integer
---@return boolean
function ImGui.BeginTable(str_id, columns, flags) end

function ImGui.EndTable() end

function ImGui.TableNextRow() end

---@return boolean
function ImGui.TableNextColumn() end

---@param column_n integer
---@return boolean
function ImGui.TableSetColumnIndex(column_n) end

---@param label string
---@param flags? integer
function ImGui.TableSetupColumn(label, flags) end

function ImGui.TableHeadersRow() end

---@param flags? integer
---@return boolean
function ImGui.IsItemHovered(flags) end

---@return boolean
function ImGui.IsItemActive() end

---@return boolean
function ImGui.IsItemEdited() end

---@return boolean
function ImGui.IsItemClicked() end

---@param str_id_begin string
---@param str_id_end string
function ImGui.PushID(str_id_begin, str_id_end) end

function ImGui.PopID() end

---@param idx integer
---@param col Vec4
function ImGui.PushStyleColor(idx, col) end

function ImGui.PopStyleColor() end

---@param idx integer
---@param val number
function ImGui.PushStyleVar(idx, val) end

---@param idx integer
---@param val Vec2
function ImGui.PushStyleVarVec2(idx, val) end

function ImGui.PopStyleVar() end

-- <<<END GENERATED IMGUI BINDINGS>>>

-- <<<BEGIN GENERATED (extract_bindings.py) -- do not edit>>>

--- `Animation` namespace (generated).
---@class Animation
Animation = {}

---@param entity any
---@param restart any
function Animation.play(entity, restart) end

---@param entity any
function Animation.pause(entity) end

---@param entity any
function Animation.stop(entity) end

---@param entity any
---@param animationUuid any
---@param restart any
function Animation.setAnimation(entity, animationUuid, restart) end

---@param entity any
---@param seconds any
function Animation.setTime(entity, seconds) end

---@param entity any
---@param normalizedTime any
function Animation.setNormalizedTime(entity, normalizedTime) end

---@param entity any
---@param speed any
function Animation.setSpeed(entity, speed) end

---@param entity any
---@param loop any
function Animation.setLoop(entity, loop) end

---@param entity any
function Animation.state(entity) end

---@param skeletonUuid any
function Animation.jointCount(skeletonUuid) end

---@param animationUuid any
function Animation.duration(animationUuid) end

---@param entity any
---@param name any
---@param value any
function Animation.setFloat(entity, name, value) end

---@param entity any
---@param name any
---@param value any
function Animation.setBool(entity, name, value) end

---@param entity any
---@param name any
function Animation.setTrigger(entity, name) end

---@param entity any
---@param name any
function Animation.getFloat(entity, name) end

---@param entity any
---@param name any
function Animation.getBool(entity, name) end

---@param luaState any
---@param entity any
function Animation.currentState(luaState, entity) end

--- `Asset` namespace (generated).
---@class Asset
Asset = {}

---@param uri any
function Asset.resolveUri(uri) end

---@param uri any
function Asset.loadText(uri) end

---@param uri any
function Asset.loadMesh(uri) end

---@param uri any
function Asset.loadTexture(uri) end

---@param uri any
function Asset.loadGaussianSplat(uri) end

function Asset.memoryStats() end

--- `Audio` namespace (generated).
---@class Audio
Audio = {}

---@param clip any
function Audio.preloadClip(clip) end

---@param clip any
---@param volume any
---@param pitch any
function Audio.playOneShot(clip, volume, pitch) end

---@param clip any
---@param position any
---@param volume any
---@param pitch any
function Audio.playOneShotAt(clip, position, volume, pitch) end

---@param clip any
---@param options any
function Audio.playMusic(clip, options) end

---@param fadeOutMs any
function Audio.stopMusic(fadeOutMs) end

---@param id any
---@param fadeOutMs any
function Audio.stopSound(id, fadeOutMs) end

---@param id any
function Audio.pauseSound(id) end

---@param id any
function Audio.resumeSound(id) end

---@param id any
---@param volume any
function Audio.setVolume(id, volume) end

---@param id any
---@param pitch any
function Audio.setPitch(id, pitch) end

---@param id any
---@param loop any
function Audio.setLooping(id, loop) end

---@param id any
function Audio.isPlaying(id) end

---@param entity any
---@param restart any
function Audio.play(entity, restart) end

---@param entity any
function Audio.pause(entity) end

---@param entity any
function Audio.stop(entity) end

---@param volume any
function Audio.setMasterVolume(volume) end

function Audio.masterVolume() end

function Audio.backendReady() end

--- `Camera` namespace (generated).
---@class Camera
Camera = {}

function Camera.count() end

function Camera.overlayInfo() end

---@param suppressed any
function Camera.setInputSuppressed(suppressed) end

function Camera.findPrimary() end

--- `Character` namespace (generated).
---@class Character
Character = {}

---@param entity any
function Character.has(entity) end

---@param entity any
---@param horizontalVelocity any
function Character.move(entity, horizontalVelocity) end

---@param entity any
---@param speed any
function Character.jump(entity, speed) end

---@param entity any
function Character.isGrounded(entity) end

---@param entity any
function Character.velocity(entity) end

---@param entity any
function Character.groundNormal(entity) end

---@param entity any
---@param position any
function Character.setPosition(entity, position) end

--- `I18n` namespace (generated).
---@class I18n
I18n = {}

---@param key any
function I18n.tr(key) end

---@param locale any
function I18n.setLanguage(locale) end

function I18n.language() end

---@param luaState any
function I18n.languages(luaState) end

---@param locale any
function I18n.displayName(locale) end

---@param enabled any
function I18n.setPseudolocalize(enabled) end

--- `Input` namespace (generated).
---@class Input
Input = {}

---@param key KeyCode
---@return boolean
function Input.isKeyHeld(key) end

---@param key KeyCode
---@return boolean
function Input.isKeyPressed(key) end

---@param key KeyCode
---@return boolean
function Input.isKeyReleased(key) end

---@param key KeyCode
---@return boolean
function Input.isKeyRepeated(key) end

---@param button MouseCode
---@return boolean
function Input.isMouseButtonHeld(button) end

---@param button MouseCode
---@return boolean
function Input.isMouseButtonPressed(button) end

---@param button MouseCode
---@return boolean
function Input.isMouseButtonReleased(button) end

---@param button MouseCode
---@return integer
function Input.mouseButtonClicks(button) end

---@return Vec2
function Input.mousePosition() end

---@return Vec2
function Input.mousePositionFlipY() end

---@return Vec2
function Input.mousePositionDelta() end

---@return Vec2
function Input.mouseScrollDelta() end

---@return boolean
function Input.isGamepadConnected() end

---@param button GamepadButton
---@return boolean
function Input.isGamepadButtonHeld(button) end

---@param button GamepadButton
---@return boolean
function Input.isGamepadButtonPressed(button) end

---@param button GamepadButton
---@return boolean
function Input.isGamepadButtonReleased(button) end

---@param axis GamepadAxis
---@return number
function Input.gamepadAxis(axis) end

---@param lowFrequency number
---@param highFrequency number
---@param durationMs integer
function Input.rumble(lowFrequency, highFrequency, durationMs) end

---@return integer
function Input.touchCount() end

---@param index integer
---@return integer
function Input.touchId(index) end

---@param index integer
---@return Vec2
function Input.touchPosition(index) end

---@param index integer
---@return Vec2
function Input.touchDelta(index) end

---@param index integer
---@return boolean
function Input.isTouchPressed(index) end

---@param index integer
---@return boolean
function Input.isTouchReleased(index) end

function Input.startTextInput() end

function Input.stopTextInput() end

---@return string
function Input.textInput() end

---@param action string
---@return boolean
function Input.hasAction(action) end

---@param action string
---@return boolean
function Input.isActionHeld(action) end

---@param action string
---@return boolean
function Input.isActionPressed(action) end

---@param action string
---@return boolean
function Input.isActionReleased(action) end

---@param action string
---@return number
function Input.actionAxis(action) end

---@param action string
function Input.clearActionEvents(action) end

---@param action string
---@param key KeyCode
function Input.bindActionKey(action, key) end

---@param action string
---@param button MouseCode
function Input.bindActionMouseButton(action, button) end

---@param action string
---@param button GamepadButton
function Input.bindActionGamepadButton(action, button) end

---@param action string
---@param axis GamepadAxis
---@param scale number
function Input.bindActionGamepadAxis(action, axis, scale) end

--- `Nav` namespace (generated).
---@class Nav
Nav = {}

function Nav.bake() end

function Nav.isBaked() end

---@param luaState any
---@param start any
---@param end any
function Nav.findPath(luaState, start, end) end

---@param point any
function Nav.nearestPoint(point) end

---@param entity any
---@param target any
function Nav.setAgentDestination(entity, target) end

---@param entity any
function Nav.stopAgent(entity) end

---@param entity any
function Nav.agentHasPath(entity) end

---@param enabled any
function Nav.setDebugDrawEnabled(enabled) end

function Nav.debugDrawEnabled() end

--- `Physics` namespace (generated).
---@class Physics
Physics = {}

function Physics.enabled() end

---@param enabled any
function Physics.setEnabled(enabled) end

function Physics.bodyCount() end

---@param entity any
function Physics.hasBody(entity) end

function Physics.fixedTimeStep() end

---@param seconds any
function Physics.setFixedTimeStep(seconds) end

---@param entity any
---@param force any
function Physics.addForce(entity, force) end

---@param entity any
---@param impulse any
function Physics.addImpulse(entity, impulse) end

---@param entity any
---@param position any
---@param activate any
function Physics.setPosition(entity, position, activate) end

---@param origin any
---@param direction any
---@param maxDistance any
---@param activeOnly any
---@param layerMask any
function Physics.raycast(origin, direction, maxDistance, activeOnly, layerMask) end

---@param luaState any
---@param origin any
---@param direction any
---@param maxDistance any
---@param activeOnly any
---@param layerMask any
function Physics.raycastAll(luaState, origin, direction, maxDistance, activeOnly, layerMask) end

---@param luaState any
---@param origin any
---@param direction any
---@param radius any
---@param maxDistance any
---@param activeOnly any
---@param layerMask any
function Physics.sphereCast(luaState, origin, direction, radius, maxDistance, activeOnly, layerMask) end

---@param luaState any
---@param center any
---@param radius any
---@param activeOnly any
function Physics.overlapSphere(luaState, center, radius, activeOnly) end

---@param luaState any
---@param center any
---@param halfExtents any
---@param activeOnly any
function Physics.overlapBox(luaState, center, halfExtents, activeOnly) end

---@param luaState any
---@param center any
---@param halfHeight any
---@param radius any
---@param activeOnly any
---@param layerMask any
function Physics.overlapCapsule(luaState, center, halfHeight, radius, activeOnly, layerMask) end

---@param luaState any
---@param activeOnly any
function Physics.contactPairs(luaState, activeOnly) end

---@param luaState any
function Physics.contactEvents(luaState) end

---@param entity any
---@param torque any
function Physics.addTorque(entity, torque) end

---@param entity any
---@param impulse any
function Physics.addAngularImpulse(entity, impulse) end

---@param entity any
---@param eulerDegrees any
---@param activate any
function Physics.setRotation(entity, eulerDegrees, activate) end

function Physics.gravity() end

---@param gravity any
function Physics.setGravity(gravity) end

---@param a any
---@param b any
---@param enabled any
function Physics.setLayerCollision(a, b, enabled) end

---@param a any
---@param b any
function Physics.layerCollision(a, b) end

---@param bodyA any
---@param bodyB any
function Physics.addFixedConstraint(bodyA, bodyB) end

---@param bodyA any
---@param bodyB any
---@param point any
function Physics.addPointConstraint(bodyA, bodyB, point) end

---@param bodyA any
---@param bodyB any
---@param minDistance any
---@param maxDistance any
function Physics.addDistanceConstraint(bodyA, bodyB, minDistance, maxDistance) end

---@param bodyA any
---@param bodyB any
---@param point any
---@param axis any
---@param minAngleDegrees any
---@param maxAngleDegrees any
function Physics.addHingeConstraint(bodyA, bodyB, point, axis, minAngleDegrees, maxAngleDegrees) end

---@param bodyA any
---@param bodyB any
---@param point any
---@param axis any
---@param minDistance any
---@param maxDistance any
function Physics.addSliderConstraint(bodyA, bodyB, point, axis, minDistance, maxDistance) end

---@param bodyA any
---@param bodyB any
---@param point any
---@param twistAxis any
---@param halfAngleDegrees any
function Physics.addConeConstraint(bodyA, bodyB, point, twistAxis, halfAngleDegrees) end

---@param constraintId any
function Physics.removeConstraint(constraintId) end

---@param constraintId any
function Physics.isConstraintValid(constraintId) end

---@param constraintId any
---@param enabled any
---@param targetVelocity any
---@param maxForce any
function Physics.setConstraintMotor(constraintId, enabled, targetVelocity, maxForce) end

--- `Render` namespace (generated).
---@class Render
Render = {}

---@param width any
---@param height any
function Render.resize(width, height) end

function Render.gaussianSplatSettings() end

---@param settings any
function Render.setGaussianSplatSettings(settings) end

function Render.gaussianSplatFrameStats() end

---@param enabled any
function Render.setProfilerEnabled(enabled) end

function Render.isProfilerEnabled() end

function Render.profilerHistorySize() end

function Render.captureFrame() end

--- `RenderBackend` namespace (generated).
---@class RenderBackend
RenderBackend = {}

function RenderBackend.isXREnabled() end

function RenderBackend.isXRMirrorEnabled() end

function RenderBackend.isExitRequested() end

--- `Save` namespace (generated).
---@class Save
Save = {}

---@param key any
---@param value any
function Save.set(key, value) end

---@param luaState any
---@param key any
---@param fallback any
function Save.get(luaState, key, fallback) end

---@param key any
function Save.has(key) end

---@param key any
function Save.remove(key) end

function Save.clear() end

---@param slot any
function Save.save(slot) end

---@param slot any
function Save.load(slot) end

---@param slot any
function Save.hasSlot(slot) end

---@param slot any
function Save.deleteSlot(slot) end

---@param luaState any
function Save.listSlots(luaState) end

--- `Scene` namespace (generated).
---@class Scene
Scene = {}

---@param uri any
function Scene.load(uri) end

---@param uri any
function Scene.instantiate(uri) end

---@param uri any
---@param parent any
---@param clearWorld any
function Scene.instantiateChild(uri, parent, clearWorld) end

---@param uri any
function Scene.saveWorld(uri) end

---@param uri any
---@param root any
function Scene.saveEntity(uri, root) end

---@param entity any
function Scene.dontDestroyOnLoad(entity) end

---@param entity any
function Scene.isPersistent(entity) end

--- `Script` namespace (generated).
---@class Script
Script = {}

---@param e Entity
---@return boolean
function Script.reloadEntity(e) end

---@return boolean
function Script.reloadAll() end

---@param e Entity
---@return boolean
function Script.hasInstance(e) end

---@param e Entity
function Script.destroyInstance(e) end

---@param playing boolean
---@param paused boolean
function Script.setPlaybackState(playing, paused) end

---@return boolean
function Script.isPlaybackPlaying() end

---@return boolean
function Script.isPlaybackPaused() end

---@param code string
---@return boolean
function Script.runString(code) end

--- `Time` namespace (generated).
---@class Time
Time = {}

---@return number
function Time.deltaTime() end

---@return number
function Time.fixedDeltaTime() end

---@return number
function Time.unscaledDeltaTime() end

---@return number
function Time.smoothedDeltaTime() end

---@return number
function Time.totalTime() end

---@return number
function Time.unscaledTotalTime() end

---@return number
function Time.averageFrameTime() end

---@return number
function Time.framesPerSecond() end

---@return number
function Time.timeScale() end

---@return number
function Time.fixedAlpha() end

---@return integer
function Time.fixedStepsThisFrame() end

---@return integer
function Time.frameIndex() end

---@return number
function Time.maxDeltaTime() end

---@param dt number
function Time.setFixedDeltaTime(dt) end

---@param scale number
function Time.setTimeScale(scale) end

---@param maxSteps integer
function Time.setMaxFixedStepsPerFrame(maxSteps) end

---@param dt number
function Time.setMaxDeltaTime(dt) end

---@param factor number
function Time.setDeltaSmoothingFactor(factor) end

--- `Upscaler` namespace (generated).
---@class Upscaler
Upscaler = {}

---@param state any
function Upscaler.providers(state) end

---@param state any
function Upscaler.active(state) end

---@param name any
function Upscaler.setActive(name) end

---@param enabled any
function Upscaler.setEnabled(enabled) end

---@param mode any
function Upscaler.setMode(mode) end

---@param state any
function Upscaler.status(state) end

--- `World` namespace (generated).
---@class World
World = {}

---@param name any
function World.create(name) end

---@param entity any
function World.destroy(entity) end

function World.count() end

---@param name any
function World.findByName(name) end

---@param luaState any
---@param prefix any
function World.findByNamePrefix(luaState, prefix) end

---@param luaState any
function World.entities(luaState) end

--- Animator usertype (generated).
---@class Animator
---@field valid boolean @ read-only
---@field skeleton any
---@field animation any
---@field playing any
---@field loop any
---@field speed any
---@field time any
local Animator = {}

function Animator:play(restart) end

function Animator:pause() end

function Animator:stop() end

function Animator:setNormalizedTime(normalizedTime) end

function Animator:state() end

--- AudioListener usertype (generated).
---@class AudioListener
---@field valid boolean @ read-only
local AudioListener = {}

--- AudioSource usertype (generated).
---@class AudioSource
---@field valid boolean @ read-only
local AudioSource = {}

--- BoxShape usertype (generated).
---@class BoxShape
---@field valid boolean @ read-only
local BoxShape = {}

--- CameraRef usertype (generated).
---@class CameraRef
---@field valid boolean @ read-only
local CameraRef = {}

--- CapsuleShape usertype (generated).
---@class CapsuleShape
---@field valid boolean @ read-only
local CapsuleShape = {}

--- CylinderShape usertype (generated).
---@class CylinderShape
---@field valid boolean @ read-only
local CylinderShape = {}

--- Entity usertype (generated).
---@class Entity
---@field valid any @ read-only
---@field id any @ read-only
---@field name any
---@field active any
---@field visible any
---@field transform any @ read-only
local Entity = {}

function Entity:destroy() end

function Entity:parent() end

function Entity:firstChild() end

function Entity:nextSibling() end

function Entity:setParent(parent) end

--- Environment usertype (generated).
---@class Environment
---@field valid boolean @ read-only
local Environment = {}

--- Light usertype (generated).
---@class Light
---@field valid boolean @ read-only
local Light = {}

--- Mesh usertype (generated).
---@class Mesh
---@field valid boolean @ read-only
---@field builtinGeometry any
local Mesh = {}

function Mesh:setMaterial(slot, uri) end

function Mesh:setMaterialFloat(slot, name, value) end

function Mesh:setMaterialColor(slot, name, value) end

function Mesh:setMaterialTexture(slot, name, uri) end

function Mesh:clearMaterialProperty(slot, name) end

function Mesh:clearMaterialProperties(slot) end

--- NavAgent usertype (generated).
---@class NavAgent
---@field valid boolean @ read-only
local NavAgent = {}

--- ParticleEmitter usertype (generated).
---@class ParticleEmitter
---@field valid boolean @ read-only
local ParticleEmitter = {}

--- ReflectionProbe usertype (generated).
---@class ReflectionProbe
---@field valid boolean @ read-only
local ReflectionProbe = {}

--- RigidBody usertype (generated).
---@class RigidBody
---@field valid boolean @ read-only
---@field linearVelocity any
---@field angularVelocity any
---@field motionType any
---@field objectLayer any
---@field isSensor any
---@field motionQuality any
---@field allowSleeping any
---@field mass any
---@field overrideMass any
---@field friction any
---@field restitution any
---@field linearDamping any
---@field angularDamping any
---@field gravityFactor any
---@field maxLinearVelocity any
---@field maxAngularVelocity any
local RigidBody = {}

function RigidBody:activate() end

function RigidBody:addForce(force) end

function RigidBody:addImpulse(impulse) end

function RigidBody:setPosition(position, activate) end

--- SphereShape usertype (generated).
---@class SphereShape
---@field valid boolean @ read-only
local SphereShape = {}

--- Transform usertype (generated).
---@class Transform
---@field position any
---@field scale any
---@field rotation any
---@field rotationEuler any
local Transform = {}

function Transform:translate(delta) end

function Transform:setEulerDegrees(value) end

function Transform:lookAt(target) end

--- Enum generated from vultra::AssetState.
---@class AssetState
---@field Unloaded integer
---@field Loaded integer
---@field LoadingCPU integer
---@field CPUReady integer
---@field UploadQueued integer
---@field UploadingGPU integer
---@field Ready integer
---@field Failed integer
AssetState = {}

--- Enum generated from vultra::CameraControlMode.
---@class CameraControlMode
---@field Disabled integer
---@field Orbit integer
---@field Fly integer
CameraControlMode = {}

--- Enum generated from vultra::ScriptComponentType.
---@class Component
---@field Transform integer
---@field RigidBody integer
---@field Camera integer
---@field Light integer
---@field Mesh integer
---@field BoxShape integer
---@field SphereShape integer
---@field CapsuleShape integer
---@field CylinderShape integer
---@field Animator integer
---@field AudioSource integer
---@field AudioListener integer
---@field Environment integer
---@field ParticleEmitter integer
---@field ReflectionProbe integer
---@field RectTransform integer
---@field UiButton integer
---@field UiToggle integer
---@field UiSlider integer
---@field UiProgressBar integer
---@field UiInputField integer
---@field UiDropdown integer
---@field NavAgent integer
Component = {}

--- Enum generated from vultra::GamepadAxis.
---@class GamepadAxis
---@field LeftX integer
---@field LeftY integer
---@field RightX integer
---@field RightY integer
---@field LeftTrigger integer
---@field RightTrigger integer
---@field Count integer
GamepadAxis = {}

--- Enum generated from vultra::GamepadButton.
---@class GamepadButton
---@field South integer
---@field East integer
---@field West integer
---@field North integer
---@field Back integer
---@field Guide integer
---@field Start integer
---@field LeftStick integer
---@field RightStick integer
---@field LeftShoulder integer
---@field RightShoulder integer
---@field DpadUp integer
---@field DpadDown integer
---@field DpadLeft integer
---@field DpadRight integer
---@field Count integer
GamepadButton = {}

--- Enum generated from vultra::GaussianSplatBaselineMode.
---@class GaussianSplatBaselineMode
---@field Baseline integer
---@field OrderedClod integer
GaussianSplatBaselineMode = {}

--- Enum generated from vultra::GaussianSplatFoveatedRenderMode.
---@class GaussianSplatFoveatedRenderMode
---@field SinglePass integer
---@field LayeredComposite integer
GaussianSplatFoveatedRenderMode = {}

--- Enum generated from vultra::KeyCode.
---@class KeyCode
---@field Unknown integer
---@field A integer
---@field B integer
---@field C integer
---@field D integer
---@field E integer
---@field F integer
---@field G integer
---@field H integer
---@field I integer
---@field J integer
---@field K integer
---@field L integer
---@field M integer
---@field N integer
---@field O integer
---@field P integer
---@field Q integer
---@field R integer
---@field S integer
---@field T integer
---@field U integer
---@field V integer
---@field W integer
---@field X integer
---@field Y integer
---@field Z integer
---@field Num1 integer
---@field Num2 integer
---@field Num3 integer
---@field Num4 integer
---@field Num5 integer
---@field Num6 integer
---@field Num7 integer
---@field Num8 integer
---@field Num9 integer
---@field Num0 integer
---@field Return integer
---@field Escape integer
---@field Backspace integer
---@field Tab integer
---@field Space integer
---@field Minus integer
---@field Equals integer
---@field LeftBracket integer
---@field RightBracket integer
---@field Backslash integer
---@field Semicolon integer
---@field Apostrophe integer
---@field Grave integer
---@field Comma integer
---@field Period integer
---@field Slash integer
---@field CapsLock integer
---@field F1 integer
---@field F2 integer
---@field F3 integer
---@field F4 integer
---@field F5 integer
---@field F6 integer
---@field F7 integer
---@field F8 integer
---@field F9 integer
---@field F10 integer
---@field F11 integer
---@field F12 integer
---@field PrintScreen integer
---@field ScrollLock integer
---@field Pause integer
---@field Insert integer
---@field Home integer
---@field PageUp integer
---@field Delete integer
---@field End integer
---@field PageDown integer
---@field Right integer
---@field Left integer
---@field Down integer
---@field Up integer
---@field NumLock integer
---@field KPDivide integer
---@field KPMultiply integer
---@field KPMinus integer
---@field KPPlus integer
---@field KPEnter integer
---@field KP1 integer
---@field KP2 integer
---@field KP3 integer
---@field KP4 integer
---@field KP5 integer
---@field KP6 integer
---@field KP7 integer
---@field KP8 integer
---@field KP9 integer
---@field KP0 integer
---@field KPPeriod integer
---@field LCtrl integer
---@field LShift integer
---@field LAlt integer
---@field LGUI integer
---@field RCtrl integer
---@field RShift integer
---@field RAlt integer
---@field RGUI integer
---@field Menu integer
KeyCode = {}

--- Enum generated from vultra::MouseCode.
---@class MouseCode
---@field Left integer
---@field Middle integer
---@field Right integer
---@field X1 integer
---@field X2 integer
MouseCode = {}

--- Value struct generated from ScriptAnimatorPlaybackState.
---@class AnimatorPlaybackState
---@field valid boolean
---@field playing boolean
---@field loop boolean
---@field speed number
---@field time number
---@field duration number
---@field normalizedTime number
---@field skeleton string
---@field animation string
local AnimatorPlaybackState = {}

--- Value struct generated from ScriptAssetHandle.
---@class AssetHandle
---@field valid boolean
---@field ready boolean
---@field uuid string
---@field state integer
---@field gpuIndex integer
local AssetHandle = {}

--- Value struct generated from ScriptAssetMemoryStats.
---@class AssetMemoryStats
---@field cpuCacheBytes integer
local AssetMemoryStats = {}

--- Value struct generated from ScriptCameraOverlayInfo.
---@class CameraOverlayInfo
---@field enabled boolean
---@field mode integer
local CameraOverlayInfo = {}

--- Value struct generated from ScriptGaussianSplatFrameStats.
---@class GaussianSplatFrameStats
---@field frameIndex integer
---@field baselineMode integer
---@field foveatedRenderMode integer
---@field lodBudgetEnabled boolean
---@field foveatedClodEnabled boolean
---@field foveatedLayeredCompositeEnabled boolean
---@field foveatedBudgetControllerEnabled boolean
---@field directPrefix boolean
---@field lodBudget integer
---@field foveatedRingLevels Vec3
---@field foveatedResolutionScales Vec3
---@field foveatedRingDegrees Vec2
---@field foveatedTargetFrameMs number
---@field splatAssets integer
---@field drawRecords integer
---@field totalSplats integer
---@field preparedSplats integer
---@field maxVisibleSplatCap integer
---@field lodSelectedRawSplats integer
---@field visibleSplats integer
---@field drawnSplats integer
local GaussianSplatFrameStats = {}

--- Value struct generated from ScriptGaussianSplatSettings.
---@class GaussianSplatSettings
---@field baselineMode integer
---@field lodBudget integer
---@field clodLevel number
---@field foveatedClodEnabled boolean
---@field foveatedRenderMode integer
---@field foveatedGaze Vec2
---@field foveatedRingDegrees Vec2
---@field foveatedRingLevels Vec3
---@field foveatedResolutionScales Vec3
---@field foveatedTransitionDegrees number
---@field foveatedBudgetControllerEnabled boolean
---@field foveatedTargetFrameMs number
---@field foveatedBudgetAdjustRate number
local GaussianSplatSettings = {}

--- Value struct generated from ScriptPhysicsContactPair.
---@class PhysicsContactPair
---@field a Entity
---@field b Entity
local PhysicsContactPair = {}

--- Value struct generated from ScriptPhysicsRaycastHit.
---@class PhysicsRaycastHit
---@field hit boolean
---@field entity Entity
---@field point Vec3
---@field normal Vec3
---@field fraction number
---@field distance number
local PhysicsRaycastHit = {}

--- Value struct generated from ScriptTextAssetResult.
---@class TextAssetResult
---@field ok boolean
---@field text string
---@field error string
local TextAssetResult = {}

-- <<<END GENERATED (extract_bindings.py)>>>
