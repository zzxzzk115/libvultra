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
--- TODO(stub): document remaining fields/methods (tracked in conformance exceptions.lua).
---@class Entity
---@field valid boolean
---@field name string
---@field transform Transform
local Entity = {}

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
