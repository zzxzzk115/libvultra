# Foundation Consolidation Roadmap (Lua API / Editor Extensibility / Plugins)

Date: 2026-06-12
Status: Done (all 6 phases landed 2026-06-13; follow-ups in Next steps)

## Summary

Full-engine audit from a game-developer perspective, plus the phased roadmap to
solidify three foundation pillars before adding advanced commercial-engine
features: (1) Lua binding API design discipline, (2) Lua-driven editor
extensibility, (3) plugin lifecycle completeness. Normative API rules live in
`doc/lua_api_design.md` (Phase 1 deliverable); this note holds the audit
findings and the phase plan.

## Audit findings (2026-06-12)

### Solid (keep building on)

- Rendering: declarative render graphs + GPU-driven pipeline + 34 builtin
  passes (~21.6k LOC); strongest subsystem.
- Asset pipeline: vasset import + VPK pack + VFS + async loading.
- Physics: Jolt with full query API (raycast/shapecast/overlap/contacts).
- i18n, editor core (17 windows, undo/redo, play-in-editor, runtime MCP).
- Plugin extension points that already work well: service registry
  (`ctx.services.tryGet<T>`), `IRenderBackendExtension` Vulkan hooks,
  `IUpscalerProvider`, scripted Lua render passes, custom material nodes.

### Flawed (the three pillars)

1. **Lua bindings** -- 15 hand-written sol2 modules
   (`source/vultra/src/function/scripting/bindings/`). Naming drift:
   `Vec2` type vs `vec2()` ctor, `rotationEuler` vs `rotationDegrees`,
   `clicked` vs `onClick` duplicate signals, three getter styles
   (`Input.getKey` / `Time.deltaTime` / `Physics.enabled`). Coverage gaps:
   Camera (only `findPrimary`), particle emitter, environment, reflection
   probes, AudioSource/Listener components, capsule/cylinder/mesh shapes.
   Latent hazard: `build/lua-stubs/vultra.lua` claims "generated" but no
   generator exists, file is not versioned, and it covers only math types
   (~5% of the live API) -- IDE hints silently drift.
2. **Editor extensibility** -- zero ImGui Lua binding; 17 editor windows are
   hardcoded C++; no registerPanel/registerInspector/registerMenuItem.
3. **Plugin lifecycle** -- native: only `install`/`uninstall` (no per-frame
   update); Lua: only `on_install`/`on_uninstall`; manifest has no
   `dependencies`, no ABI version check.

### Missing entirely (post-foundation backlog, not in this roadmap)

Networking, game-save system (scene serialization only), navigation/
pathfinding, gamepad input, in-game text rendering, IK / animation event
tracks.

## Roadmap

Decisions locked with the owner (2026-06-12):

- Renames ship as **alias + one-time deprecation warning for one release**,
  then deleted; in-repo scripts migrate in the same PR.
- ImGui binding is **generated from dear_bindings JSON** (not hand-written,
  not vendored sol2_ImGui_Bindings).
- **Binding generator** (python + libclang) is Phase 2's first step, not
  Phase 1: annotation-driven (`VLUA_*` attribute macros carry the Lua-facing
  name/property decisions), parses via `compile_commands.json` from
  `xmake project -k compile_commands`, and emits three artifacts from one
  source of truth: sol2 registration `.gen.cpp` (checked in), LuaLS stub,
  and the conformance surface list. Hand-written modules remain for shaped
  APIs (options tables, signals, entity handles); the generator covers the
  mechanical property/method majority. Pilot target: the new Camera binding.

### Phase 1 -- spec + conformance infrastructure (CURRENT)

- `doc/lua_api_design.md`: normative naming/property/unit/error/options/
  signal/deprecation/coverage rules + review checklist (referenced from
  `AGENTS.md`).
- `tests/lua_api_conformance/`: headless sol::state + null-service
  `ScriptContext`, runs `conformance.lua` -- introspects the live API
  surface, asserts naming rules, diffs against the versioned stub.
  Existing violations live in a checked-in `exceptions.lua` burn-down list
  (test is green from day one; stale-stub symbols are hard failures).
- Stub promoted into the repo at `tools/lua-stubs/vultra.lua`.

### Phase 2 -- generator pilot + mechanical cleanup + coverage fill (DONE 2026-06-13, partial coverage)

Landed:
- Renames + central deprecation shim
  (`bindings/script_deprecations.cpp`, runs last in registerScriptBindings,
  publishes `__vultraDeprecated` registry the conformance checker exempts):
  `vec2/3/4` -> callable `Vec2/3/4` (required adding `sol::call_constructor`
  -- the call-ctor form NEVER actually worked before; caught by the new
  behavior asserts), `Input.get*` -> `isKeyHeld/isKeyPressed/isKeyReleased/
  isKeyRepeated/isMouseButton*/mouse*`, `Animation.getFloat/getBool` ->
  `floatParam/boolParam`, `Render.getGaussianSplat*` -> `gaussianSplat*`,
  `Transform.rotationEuler`+`RectTransform.rotationDegrees` -> `rotation`,
  `UiButton/UiToggle.clicked` -> `onClick` (old names warn once from C++ via
  `script_binding::warnDeprecated`).
- libclang generator pilot (`tools/python/gen_lua_bindings.py` + `VLUA_*`
  macros in `core/base/lua_annotations.hpp`): CameraRef fully generated from
  annotated `camera_component.hpp` (manual registration deleted from
  script_world_binding.cpp); added zNear/zFar/clearMode/clearColor/priority;
  `fovYDegrees` -> `fovY` with generated deprecated alias. Note: needs
  `-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH` (pip libclang vs MSVC STL).
- In-repo Lua + doc/lua_scripting.md migrated to canonical names.
- Baseline 355 -> 309; all 16 naming violations burned. Remaining 309 are
  stub.missing coverage debt only.

Still open for Phase 2 follow-up: particle/environment/reflection-probe/
audio-component/remaining-shape bindings; stub coverage burn-down.

### Phase 3 -- script lifecycle completion (DONE 2026-06-13)

- `OnEnable`/`OnDisable` dispatched on enabled transitions; guaranteed order
  `OnCreate -> OnEnable -> updates -> OnDisable -> OnDestroy`
  (`script_system.cpp::setInstanceEnabled`).
- `OnCollisionEnter/Stay/Exit` + `OnTriggerEnter/Stay/Exit` delivered to
  scripts on both entities, via `contactPairs()` frame diff in
  `ScriptSystem::dispatchContactCallbacks` -- deliberately NOT
  `consumeContactEvents()`, which is a drain API with a single consumer;
  `Physics.contactEvents()` polling keeps working. Sensor-ness captured at
  enter so exits classify correctly after component removal.
- Coroutines: pure-Lua scheduler in `bindings/script_coroutine_binding.cpp`
  (part of registerScriptBindings, NOT ScriptEngine, so the headless
  conformance harness exercises it); env-level `startCoroutine/
  stopAllCoroutines`, global `wait/waitFrames`, error-raising global
  fallbacks. Functionally verified in the conformance test (first-slice,
  wait, waitFrames, stopAll, fallback errors).
- Verification level: coroutines behavior-tested headless; OnEnable/contact
  callbacks compile-verified + code-reviewed only (need a world+physics
  fixture or editor MCP smoke for runtime proof).

### Phase 4 -- ImGui Lua binding (DONE 2026-06-13)

- dear_bindings run locally against the exact engine imgui.h (v1.92.5-docking
  from the xmake package) -> `build/.tmp/cimgui.json`;
  `tools/python/gen_imgui_lua.py` (allowlist embedded in the script) emits
  `script_imgui_binding.gen.cpp`: 79 functions + 17 enum tables
  (`ImGui.WindowFlags.NoTitleBar` style). Regeneration steps in the script
  docstring.
- Hand-written `script_imgui_binding.cpp`: out-of-frame guard
  (`imgui_lua_detail::ensureFrame`, uses imgui_internal WithinFrameScope) +
  buffer-based `InputText`/`InputTextMultiline` returning
  `changed, newText`.
- Out-params become extra returns (`local visible, open = ImGui.Begin(...)`);
  ImVec2/4 map to Vec2/Vec4 usertypes.
- Registered only when `IImGuiService` is in ScriptContext (new member,
  wired in script_system.cpp); headless runtimes have no ImGui global.
  Conformance checker treats ImGui as a conditional top (skips stale-stub
  when absent, exempts upstream PascalCase when present); harness
  force-registers the generated table and asserts registration + enum values
  + out-of-frame guard raising.

### Phase 5 -- editor extension API (DONE 2026-06-13)

- Engine side: `IEditorExtensionService` (services/) + `EditorExtensionRegistry`
  (function/editor/), owned and provided by ScriptSystem (registrations come
  from Lua); `Editor` Lua table (script_editor_binding) bound only when the
  service is present -- `Editor.registerPanel{...}` / `registerMenuItem{...}`
  (return ok,err), `unregister(id)`; `registerInspector` reserved (returns
  false). Per-plugin owner tagging: PluginSystem sets the owner around
  `on_install`, and `unregisterOwned(id)` tears panels/menu items down on
  unload/shutdown.
- Editor side: `ScriptedEditorWindow : EditorWindow` (Begin/End + Lua onDraw
  via ImGui bindings; refreshLocalization made virtual for literal titles);
  `EditorWindowManager` runtime `addWindow(unique_ptr)/removeWindow/hasWindow`;
  `EditorApp::syncScriptedPanels` drains takeAddedPanels/takeRemovedPanelIds
  each tick; top bar renders `menuItems()` nested by "/"-path under Tools.
- VERIFIED via editor MCP screenshot (offscreen): example plugin
  `resources/plugins/editor_panel` (enabled in example.vproject) shows a "Lua
  Demo Panel" with text/buttons/slider/checkbox/colored text + camera fovY
  readout, plus a Tools > "Lua Examples/Bump Counter" item. No onDraw errors.
  Deferred from plan: `Editor.history:commit` undo/redo and inspector drawers.

### Phase 6 -- plugin lifecycle v2 (DONE 2026-06-13)

- `EnginePlugin::update(ctx, dt)` (default no-op) + `kEnginePluginAbiVersion`
  (=2) and exported `vultraPluginAbiVersion()`; PluginManager rejects ABI
  mismatch with an actionable error (absent symbol = legacy v1). PluginManager
  gains `update(ctx, dt)`. Example native plugins (native_math, noop_upscaler)
  export the ABI fn.
- `PluginSystem::onUpdate` ticks native plugins then Lua `on_update(dt)`.
- Manifest `dependencies` (id list) parsed; PluginSystem DFS topological sort
  so deps install first; missing/disabled dep warns (fails only that plugin),
  cycles broken with a warning. Semver ranges deferred (ids matched exactly).
- Deferred from plan: string-keyed event bus, Lua data-components, native hot
  reload, sandboxing.

## Result

All 6 phases landed 2026-06-13 (see per-phase DONE notes above). Final state:
`test-lua-api-conformance` PASS (70 roots / 303 members, 303 baselined, all
stub.missing coverage debt), full `vultra-app` editor build green, Phase 5
verified by editor MCP screenshot. Build integration: `xmake codegen` task +
`lua-codegen` phony target run the generators before `vultra` compiles, inside
a project-local `.venv` provisioned from `tools/python/requirements.txt`
(stamp-guarded so incremental builds skip the ~10s libclang pass;
write-if-changed keeps .gen.cpp mtime stable). Decision honored from the
owner: C++ and Lua names match -- `IInputService` getters renamed
engine-wide (isKeyHeld/isKeyPressed/...), `CameraComponent::fovY`,
`RectTransformComponent::rotation` (legacy .vscn keys kept as read aliases in
scene_reflection.cpp); `getFloat/getBool` kept (symmetric-pair rule).

Phase 1 landed 2026-06-12:

- `doc/lua_api_design.md` (normative spec + review checklist; referenced from
  `AGENTS.md` Gameplay API Parity and `doc/lua_scripting.md`).
- `tests/lua_api_conformance/` (target `test-lua-api-conformance`): headless
  binding registration + introspection checker + stub diff. Verified:
  - pristine run: PASS, surface = 58 roots / 289 members, 355 baselined
    violations (16 `naming.getPrefix` -- the `Input.get*` family,
    `Animation.getBool/getFloat`, `Render.getGaussianSplat*` -- plus 339
    `stub.missing` coverage gaps; this is the Phase 2 burn-down list).
  - negative checks all fail correctly: stale stub symbol (hard fail),
    removed baseline entry (new-violation fail), fabricated baseline entry
    (stale-exception fail).
- `tools/lua-stubs/vultra.lua` versioned (was unversioned in `build/`,
  falsely claimed to be generated).
- Baseline regen: `--dump | Out-File -Encoding ascii` (PowerShell `>` writes
  UTF-16, which Lua cannot parse).
- Known checker limits (documented in conformance.lua): sol2 usertype
  *instance* members are not enumerable from Lua, so usertype property
  coverage/stale checks are skipped; property-vs-method and boolean-return
  rules are review-checklist-only. The Phase 2 libclang generator removes
  both limits by emitting the surface list from source.

## Next steps

- Burn down the 303 stub.missing baseline entries (Audio/Asset/Animation/
  enum members etc.) toward an empty exceptions.lua.
- Runtime smoke for OnEnable/OnDisable + collision callbacks (editor MCP or
  a world+physics native test fixture) -- still code-review-verified only.
- Editor extension follow-ups: `Editor.registerInspector` (component drawers),
  `Editor.history:commit` undo/redo, gizmo registration.
- Plugin follow-ups: semver version ranges for dependencies; string-keyed
  event bus; Lua data-components.
- One release later: delete the deprecation aliases per policy
  (script_deprecations.cpp + the legacy .vscn read aliases).

## Blockers

none
