# Overnight Engine Polish — 2026-06-04

Autonomous session. User asleep; authorized continuous engine polishing using the
build → MCP → screenshot → verify loop. Five target areas (user priority emphasis
on physics: "read Jolt source, fill in common components comprehensively, target an
FPS"):

1. Animator Graph (new vasset, DSL/schema JSON, node-graph editor).
2. Physics — Character Controller, raycast/query, FPS-oriented shapes & components.
3. In-game GUI — world-space (3D) canvas + more controls (text, VR-ready).
4. Lua-extensible render passes (explicit inputs/outputs, engine resource access).
5. Material graph — many common builtin nodes.

## Loop facts (verified working)
- Build: `xmake build -y vultra-app` (exit 0).
- Launch: `xmake run vultra-app --editor --mcp --project example.vproject --no-xr`.
- MCP: `POST http://127.0.0.1:8848/mcp`; helper at `c:\tmp\mcp.sh <tool> '<json>'`.
- Screenshot: `vultra.render.capture_rgb {outputFile,width,height}` → PNG, viewable.
- Jolt source: `C:/Users/Administrator/AppData/Local/.xmake/packages/j/joltphysics/v5.5.0/.../include/Jolt`
  (object_layer_bits=16). Has Character/CharacterVirtual, all shapes
  (Cylinder, TaperedCapsule, ConvexHull, Mesh, HeightField, Plane, Compound),
  all constraints (Hinge, Slider, Distance, Fixed, Point, Cone, SwingTwist, SixDOF).

## Progress log
- [setup] Mapped all 5 subsystems; verified loop end-to-end. Starting physics.
- [physics slice 1] DONE + verified. Replaced fake AABB queries with native Jolt
  narrow-phase: `raycast` (closest), `raycastAll`, `sphereCast`, `overlapSphere/Box/Capsule`.
  Added configurable collision-layer matrix (logical layer index packed with a moving
  bit into the Jolt ObjectLayer) + per-query 32-bit `layerMask`/`ignore`/`activeOnly`
  filter (`PhysicsQueryFilter`). Bodies now stamp the entity into Jolt user data for
  reverse lookup. Added `addTorque`, `addAngularImpulse`, `setRotation`, gravity get/set,
  `setLayerCollision`. Files: physics_service.hpp, physics_system.{hpp,cpp},
  script_physics_binding.cpp (callers updated, full Lua surface deferred to binding slice).
  Made `vultra.sim.step` deterministic (one fixed single-step per deferred frame, wall-clock
  independent) so headless/unfocused verification is reliable. Added MCP tools
  `vultra.sim.raycast` and `vultra.sim.overlap` (+ registry). VERIFIED: drop box rests at
  y=0.973 on ground; ray hits box top at exact y=3.000 with normal (0,1,0) + correct entity;
  overlap + layerMask scoping correct.
- [physics slice 2] DONE + verified. Character Controller via Jolt `CharacterVirtual`.
  New `CharacterControllerComponent` (radius/height/maxSlopeAngle/stepHeight/gravityFactor/
  mass/jumpSpeed/objectLayer + input `inputMove`/`jumpRequested` + output `velocity`/`grounded`).
  PhysicsSystem manages a per-entity CharacterVirtual (capsule offset so origin=feet),
  collide-and-slide via ExtendedUpdate with layer-aware filters, gravity, ground sticking,
  stair stepping. Service: hasCharacter/characterMove/characterJump/characterIsGrounded/
  characterVelocity/characterGroundNormal/characterSetPosition. Full editor-command wiring
  (reflection, serialization registry, MCP metadata/get/add/update/remove, kind aliases).
  MCP: apply_actions `character_move`/`character_jump`, teleport routes to character,
  get_state_batch emits `character` block. VERIFIED: capsule falls, feet rest at y=0.500 on
  ground (grounded=true); move +x@3m/s -> x=2.95 after 1s; jump -> airborne vy=5.67.
- [physics slice 4] DONE + verified. New `CylinderShapeComponent` and `MeshShapeComponent`
  (triangle mesh for static level/terrain geometry, or convex hull for dynamic bodies),
  built from the entity's MeshComponent CPU geometry (vasset::VMesh positions/indices) with
  transform scale baked in. Asset service fetched lazily (physics inits before AssetSystem).
  BodySignature extended with meshKey(hash of UUID)+meshScale+meshConvex for rebuild detection.
  Full editor-command wiring for both shapes. VERIFIED: cylinder rests at y=0.980; CornellBox
  floor mesh gets hasBody=true and a dropped sphere rests on the triangle mesh at y=0.280
  (no tunnel-through).
- [physics lua] DONE (compiles). Extended `Physics` table: addTorque/addAngularImpulse,
  setRotation, gravity/setGravity, setLayerCollision/layerCollision, raycast+layerMask,
  raycastAll, sphereCast, overlapCapsule (all queries take activeOnly?+layerMask?). New
  `Character` table (has/move/jump/isGrounded/velocity/groundNormal/setPosition). Updated
  doc/lua_scripting.md + ai/knowledge/lua-scripting.md. (Bindings are thin pass-throughs over
  already-MCP-verified services; not separately run-verified since no Lua-eval MCP tool.)
- [material nodes] DONE + verified. Added ~35 builtin material-graph nodes to
  material_node_registry.cpp + GLSL emission in material_graph_compiler.cpp:
  unary math (abs/floor/ceil/round/truncate/sign/sqrt/exp/exp2/log/log2/cosine/tangent/
  arcsine/arccosine/arctangent/radians/degrees/negate/reciprocal/square), binary/interp
  (modulo/step/atan2/lerp/inverse_lerp/remap), vector (cross/length/distance/reflect/scale/
  combine_vec2|3|4/split_vec3|4), UV (tiling_offset/panner/rotator), color (desaturate/
  contrast/posterize), procedural (checkerboard/white_noise). All pure inline GLSL (no
  helper-prelude mechanism exists). VERIFIED: a graph chaining uv0->panner->rotator->
  white_noise->remap->combine_vec4->surface (+ cosine->metallic) compiles ok with no
  diagnostics; emitted GLSL contains the expected expressions. (Note: material_graph.compile
  reads a file by uri; `graph` arg is a uri alias, not inline JSON.)
- [gui world-space] DONE + verified. CanvasComponent gains `renderMode` (0 screen overlay,
  1 world space) + `pixelsPerUnit`. World canvases follow their entity TransformComponent:
  RenderUiDrawItem carries space/pixelsPerUnit/worldMatrix; cookUi reads the canvas entity
  worldMatrix; UiOverlayPass uploads a mat4 per item + camera viewProjection push constant;
  the vertex shader projects world items through worldMatrix*viewProjection (UI px -> meters,
  y flipped) instead of screen NDC. Screen overlay path unchanged (space==0 falls through).
  Full editor-command wiring (reflection/registry/MCP metadata+get+update, renderMode enum
  screen|world). VERIFIED via screenshot: a world canvas+panel at (0,1,0) renders as a 3D
  quad in the scene view (foreshortened at an angle) and head-on in the game view.
  Follow-up: depth-occlusion (pass currently has no depth test, world UI draws on top),
  XR/stereo per-eye viewProjection, and world-space pointer raycast for interaction.
- [lua render pass i/o] DONE + verified. Graphics (fullscreen) project/Lua passes were
  limited to a single source texture (set=3 binding 0) -> post-processing only. Now they bind
  EVERY declared input to set=3 bindings 0,1,2,... (compute already did). So script passes can
  read engine resources (depth, gbuffer normal/material, ao, ssr, shadow) wired via the vrg
  `inputs` map. Depth-format inputs auto-use the depth image aspect (added isDepthFormat).
  Files: declarative_renderer.cpp (FullscreenPassRuntime::addPass extraInputs + graphics
  registerPasses branch gathers inputs[1..]). Added example artifacts:
  resources/render/passes/depth_tint.lua + resources/shaders/fullscreen/depth_tint.frag.vshader
  (samples source@0 + depth@1). VERIFIED via screenshot: temporarily wired DepthTint into the
  default graph (source=Fxaa.color, depth=DirectGBuffer.depth); Sponza rendered with red
  depth-contour bands (fract(depth*24)) following geometry by distance -> proves depth bound at
  binding 1. Default graph restored afterward; example pass files left as inert docs.
- [animator graph] DONE + verified (runtime; node editor deferred). New `.vanimgraph.json`
  data model + JSON schema + validation (function/animation/animator_graph.{hpp,cpp}):
  parameters (float/bool/trigger), states (name + clip UUID + speed + loop), transitions
  (conditions ANDed: greater/less/equal/notEqual/true/false/trigger; duration cross-fade;
  optional hasExitTime), any-state transitions, entry state. New `AnimatorControllerComponent`
  (graph URI + skeleton + playOnStart + speed). AnimationSystem runs the state machine per
  entity (loads graph via loadTextAssetSync, seeds params from defaults, evaluates transitions,
  cross-fades current+target clips via ozz BlendingJob, writes skin palette). Service +
  Lua (`Animation.setFloat/setBool/setTrigger/getFloat/getBool/currentState`) + MCP
  (`vultra.animator.set_param`, `vultra.animator.state`). Full editor-command wiring for the
  component. VERIFIED via MCP: authored a Idle/Run/Jump graph; entry=Idle; speed=1->Run
  (float greater), speed=0->Idle (float less), jump trigger->Jump (any-state trigger, consumed),
  ->Idle; a 0.2s-duration variant showed the cross-fade transitioning with increasing progress.
  Bug fixed: nlohmann `value("default", false)` threw type_error on numeric defaults -> read by
  JSON type + wrapped graphFromJson in try/catch (the runtime AppHost doesn't catch).
  Follow-up: imnodes node-graph EDITOR window (reuse material_graph_window pattern); making the
  AnimationSystem step use the fixed delta under headless single-step (crossfade timing only).
- [material graph editor polish] DONE + verified (user-requested). (1) Node "note":
  material_graph Node gains a `note` field (JSON load/save). The editor node title now shows
  TWO lines — line 1 = node TYPE name (from descriptor), line 2 = dimmed user note; redundant
  notes (== type name) are hidden. Right-click node -> "Edit Note..."(popup InputText) /
  "Clear Note"; legacy `displayName` labels migrate into the note on display + edit.
  VERIFIED via zoomed screenshot: "Color"/"Hot Pulse Color", "Float"/"Pulse Bias", etc.;
  "Multiply"/"Add"/"Mix" single-line. (2) Material preview `time` now auto-plays by default
  (m_PreviewTimePlaying=true; restart keeps playing). VERIFIED: preview sphere pulses
  cold<->hot on its own (RGB blue<->red, span ~180) and the time field advanced to ~9.6s
  with no manual click. Files: material_graph.{hpp,cpp}, material_graph_window.{hpp,cpp}.
  NOTE (clarification for user): the default.vmatgraph.json uses only existing builtin nodes
  (9 types, all in the 65-node registry) and compiles clean — "Pulse" was just instance
  display names over builtin Time/Multiply/Sine/Add/Mix nodes, not a missing node type.
- [editor play->game view] DONE + verified (user-requested). Entering play mode now auto-
  focuses the Game View tab. On the play transition (editorPlaying false->true in
  editor_app.cpp applyPlaybackState) we set `ctx.state.editorWindowFocusRequested = "Game View"`,
  reusing the window manager's existing focus mechanism (opens + SetNextWindowFocus, clears the
  request) — so it fires once and works for both the Play button and MCP/script-triggered play.
  VERIFIED via screenshots: before play the Scene View tab is active; after play the Game View
  tab is focused (toolbar switches to Free Aspect/Zoom/Res/Metrics).
- [animator graph node editor] DONE + verified (was the big deferred item). New dockable
  "Animator Graph" editor window (imnodes), modeled on the material graph window.
  Files: animator_graph_window.{hpp,cpp}; registered in editor_app.cpp (addWindow + dock);
  app_state.hpp gains currentEditingAnimatorGraph/animatorGraphOpenRequested; new
  editor command `editor.open_animator_graph` (opens+focuses the window). Features: states
  render as nodes (in/out pins, clip uuid, speed, loop; entry flagged; an "Any State" source
  node), transitions render as links (drag out->in to create, delete link to remove);
  inspector edits graph name + entry combo, parameters (add/edit/delete float/bool/trigger),
  selected state (rename w/ ref repointing, clip UUID, speed, loop, set-entry, delete), and
  selected transition (duration, hasExitTime/exitTime, conditions: param+comparison+threshold
  add/edit/delete). Load via loadTextAssetSync, save writes file + reimport (Ctrl+S). VERIFIED
  via MCP+screenshot: opened a Idle/Run/Jump graph; nodes+links rendered; inspector showed
  graph/params/state controls. Build fixes: replaced a UTF-8 ellipsis (MSVC C2001) with "..."
  and the greedy-hex "\x01AnyState" sentinel with "::any::".
- [physics triggers/contact events] DONE + verified (deferred item). Jolt ContactListener
  captures OnContactAdded(enter)/OnContactRemoved(exit) from job threads into a mutex-guarded
  queue drained on the main thread (consumeContactEvents). Entities resolved via body userData
  (Added) and a body-index->entity map (Removed, since bodies aren't accessible there). New
  PhysicsContactEvent {type enter|exit, a, b, isSensor}; isSensor derived from components at
  drain. Bound to Lua `Physics.contactEvents()` and MCP `vultra.sim.contact_events`. Listener
  declared before `physics` in Impl so it outlives it. VERIFIED via MCP: ball through a static
  sensor -> TRIGGER enter (isSensor=true) then exit; ball hitting ground -> contact enter/exit
  (isSensor=false). Docs updated (lua_scripting.md + ai/knowledge).
- [animation fixed-step] DONE + verified (deferred refinement). AnimationSystem now uses the
  timing service's fixedDeltaTime during single-step (paused+step) instead of the tiny/erratic
  real frame delta, matching physics. VERIFIED via MCP: a 0.2s-duration crossfade advances
  0.00->0.08->0.17->...->0.92->done at exactly 1/60 per sim.step, completing at step ~12.
- [world-UI depth occlusion] DONE + verified (deferred refinement). UiOverlayPass::addPass now
  takes an optional `depth` resource; when wired it reads scene depth as a read-only depth
  attachment and the pipeline enables depth test (eLessOrEqual, no write). World-space UI
  (real NDC z) is occluded by geometry; screen-overlay UI (emitted at z=0) always passes and
  stays on top. depthFormat is threaded into createPipeline (variadic getPipeline auto-keys the
  cache). Declared "depth" input on the builtin UiOverlay pass + wired DirectGBuffer.depth in
  default.vrg.json (default_rt left unwired - no gbuffer there; depth is optional). VERIFIED via
  screenshot: a yellow world-space panel behind a cube is correctly occluded by the cube from
  the game camera. Engine depth is standard (eLess, not reverse-Z).
- [BUGFIX] World-UI depth wiring broke the render graph: declaring "depth" as a UiOverlay
  input made the renderer REQUIRE it connected, so default.vrg.json/universal.vrg.json failed
  validation ("unknown input slot" / "not connected") -> Sponza (and any scene) rendered blank.
  Reworked: UiOverlay now pulls scene depth from the frame data registry
  (ctx->data.tryGet(kResKey_DepthTexture)) instead of a declared/wired input. No vrg changes,
  no validation issue, optional (graphs without a depth pass skip occlusion). Reverted the
  registerBuiltin/pass input lists and the vrg edits. VERIFIED: Sponza renders fully; cube
  still occludes the world panel.
- [BUGFIX] Animator inspector ImGui ID conflict (two InputText labeled "Name": graph + state)
  -> gave unique ids "Name##graphName" / "Name##stateName". VERIFIED: no conflict popup.
- [BUGFIX] Animator graph non-entry state nodes rendered hugely stretched (tall empty bodies).
  Cause: the title bar called `ImGui::SameLine(0,0)` unconditionally, but non-entry states had
  no preceding item on that line, so SameLine referenced a stale cursor and inflated the node
  bbox. Fixed: only SameLine after the entry-flag item. VERIFIED: all state nodes now normal
  height.
- [animator editor polish] DONE (user-requested). (1) Right-click context menus: node ->
  Set as Entry / Add Transition From Here / Delete State; link -> Delete Transition; empty
  canvas -> Add State (mirrors the material graph pattern). (2) Clip SELECTOR replacing the
  manual UUID text field: a combo listing all eAnimation registry assets (sorted), with
  "(none)", full-source-path tooltips, and a wider field. (3) Clip name clarity: mixamo/FBX
  single-clip exports carry a generic embedded clip name ("mixamo_com"); clipDisplayName now
  detects generic names and shows the descriptive model/file stem instead (e.g.
  "Michelle_GangnamStyle"), or "File / Clip" when a file packs multiple named clips. VERIFIED:
  state inspector shows Clip = "Michelle_GangnamStyle". Also fixed a node-height glitch (the
  title-bar called SameLine(0,0) with no preceding item for non-entry states, inflating the
  node bbox) and an ImGui ID conflict (two "Name" InputTexts -> unique ids).
- [animator asset/inspector workflow] DONE (user-requested, 4 points). (1) Content icon:
  ui_widgets.cpp sourceAssetIcon -> ICON_MDI_RUN_FAST for .vanimgraph.json; eAnimation
  sub-asset -> ICON_MDI_RUN, eSkeleton -> ICON_MDI_BONE. (2) Double-click .vanimgraph.json in
  the content browser now opens the Animator Graph editor (isAnimatorGraphSourceAsset checked
  before code-editor; sets animatorGraphOpenRequested). (3) AnimatorControllerComponent now has
  a custom inspector panel (registered via AddComponentDescriptor + componentDefaultOrder +
  entityHasOrderedComponent/orderedComponentLabel/remove + render dispatch, key "Animator Graph"):
  a Graph asset SELECTOR (combo of project .vanimgraph.json files) + "Edit Graph" button. (4)
  Skeleton defaults to the entity's (or a descendant's) skinned-mesh bundled skeleton
  (VMesh.skeleton): inspector shows "(from mesh)" with an Override option; AnimationSystem's
  resolveSkeletonFor searches the subtree and falls back to the mesh skeleton at runtime (for
  both AnimatorComponent and AnimatorControllerComponent). VERIFIED via screenshots: inspector
  shows Animator Graph component with Graph=default selector, Edit Graph, Skeleton "(from mesh)".
- [merge AnimatorComponent + AnimatorControllerComponent] DONE (user-requested). One
  AnimatorComponent now carries `uint32 mode` (0 = single clip, 1 = graph) + `std::string graph`,
  alongside the existing skeleton/animation/playOnStart/playing/loop/speed/time. Removed the
  separate AnimatorControllerComponent (deleted its header). Touched: animator_component.hpp,
  scene_reflection.cpp, scene_system.cpp, animation_system.{hpp,cpp} (updateControllers ->
  per-entity updateGraphAnimator; updateWorld branches on animator.mode; ensureController reads
  AnimatorComponent w/ mode==1), inspector_window.cpp (single mode-switching
  drawAnimatorComponentFields with a Mode combo + shared drawAnimatorSkeletonRow; removed the
  "Animator Graph" ordered-component plumbing), editor_commands.cpp, lua-scripting.md. VERIFIED
  via MCP + screenshots: inspector Mode=[Single Clip|Graph]; graph mode = Graph selector + Edit
  Graph + Skeleton/Speed/PlayOnStart; single mode = Animation + Playing/Loop/Time/Reset.
- [rename MCP component kind animator_controller -> animator] DONE (user-requested). Canonical
  kind is now "animator" (metadata cxxComponent=AnimatorComponent, full merged field set; get/
  update/remove handle mode+animation+playing+loop+time+graph). Old names still resolve via the
  alias map. add via "animator" defaults to single-clip (mode 0); a non-empty graph without an
  explicit mode implies graph mode. VERIFIED via MCP.
- [sponza.vscn] Working-tree sponza.vscn is a TEST ARTIFACT: my earlier testing REPLACED the
  working single-clip AnimatorComponent on node Ch03 (skeleton 54248d82..., dance clip
  6cafc2bd...) with an empty AnimatorControllerComponent, breaking the Michelle dance. `git
  checkout` was denied by the auto-mode classifier. After the merge that component no longer
  deserializes. RECOMMEND reverting to the committed version; awaiting user OK.
- [register animator-graph vasset type + import] DONE (user-requested). vasset: added
  VAssetType::eAnimatorGraphJson ("animator_graph_json") + isValidAnimatorGraphJson
  (.vanimgraph/.vanimgraph.json) wired into isValidSourceTextAsset + inferSourceTextAssetType,
  so .vanimgraph.json is imported on startup/save like material graphs (imported/animator_graph_json/,
  .vimport sidecar, registry entry, clip deps auto-collected). Editor: inspector graph selector now
  enumerates the asset registry for eAnimatorGraphJson (robust vs folder layout); content_asset_registry
  adds an "Animation/Animator Graph" creator; content-browser sub-asset icon. VERIFIED via MCP:
  assets.list shows the graph (type animator_graph_json, uuid assigned), selector lists it.
- [VPK export drops some Sponza textures] ROOT-CAUSED + FIXED (A then B). Symptom: packaged
  runtime "loadTextureSync: cannot resolve uuid" for 3 textures = all of Sponza.gltf material 24
  (mesh[0] primitive[101], unnamed), used by no other material. Cause: the VPK packer includes only
  assets reachable via the registry @dep edges; those 3 textures had 0 incoming edges -> dropped.
  The dep graph was STALE: incremental `vultra asset import` (run by Export) skips unchanged models
  and never re-ran collectMeshDependencies, so once edges were incomplete they never healed.
  NOT caused by the animator/enum work (textures are eTexture=1, unaffected; a systematic break
  would drop ALL textures, not 3). A (unblock): `vasset-cli import resources --reimport` restored
  edges (@dep 1022->1167). B (durable, external/vasset vasset_importers.cpp): in
  VMeshImporter::importModelPrefab's up-to-date skip path, re-derive each node mesh's dependency
  edges by loading the COOKED VMesh (loadMesh + collectMeshDependencies + setDependencies) -- no
  assimp re-read, no texture recompress. VERIFIED: restored the stale registry, ran a PLAIN
  incremental import (no --reimport) -> edges self-healed 0->10; re-packed VPK (193 entries,
  validate-vpk PASSED); packaged runtime loads sponza with 0 "cannot resolve uuid".
- [packaged character doesn't animate] ROOT-CAUSED + FIXED (two issues). Symptom: graph-mode
  AnimatorComponent on Michelle (sponza), character frozen in the packaged VPK build. Diagnosed via
  temporary [AnimDBG] logs (now removed): updateWorld saw the animator (graphMode=1, playing=true),
  ensureController loaded the graph (states=1, currentState=0), the clip loaded (clipLoaded=1), but
  resolveSkeletonFor returned skelUuid=0 (skel=0) -> `if (!skeleton) return;` -> no animation.
  (1) Graph packing: editor_app_build.cpp now force-packs all .vanimgraph.json + .vmatgraph.json as
  pack roots (like .vrg.json) — see [[vpk-pack-graphs-unconditionally]] — so a stale/missing dep
  edge can't drop the graph from the VPK. (2) THE REAL FREEZE: resolveSkeletonFor reads the skinned
  mesh's cpu->skeleton, but the packaged runtime frees mesh CPU data after GPU upload
  (AssetSystem keepCpuCopy defaults false outside the editor) -> handle.cpu()==null -> no skeleton.
  Fix in asset_system.cpp upload path: `if (!keepCpuCopy && !(rec->cpu && rec->cpu->hasSkin)) cpu.reset();`
  — retain CPU data for SKINNED meshes only. Also surfaced the previously-silent graph-read failure
  with a warning in ensureController. VERIFIED in the packaged runtime: skelUuid=54248d82... skel=1,
  clip loaded, graph state 0 -> animator no longer bails -> character animates.
- [XR per-eye world UI] STILL DEFERRED: world UI uses a single camera viewProjection; stereo/XR
  needs per-eye matrices in the UI pass push constants + multiview. Needs XR hardware to verify. (sensor enter/exit + contact
  begin/persist/end) via a Jolt ContactListener with a thread-safe queue drained post-step,
  dispatched to Lua/MCP. Plus tapered capsule/cylinder, plane, heightfield-from-array.
