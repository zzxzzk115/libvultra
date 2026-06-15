# Render Graphs

**English** | [简体中文](zh_CN/render_graphs_CN.md)

Vultra uses an SRP-style (Scriptable Render Pipeline) renderer architecture: every
camera is rendered by a *renderer* that is, at its core, a declarative **render graph**
authored in a `.vrg.json` file. This document is the high-level overview of how that
fits together and how to author the graph files.

For the deep-dives this page complements, see:

- [`gpu_driven_pipeline.md`](gpu_driven_pipeline.md) — the GPU-driven mesh/meshlet
  pipeline and frame-graph internals that the builtin passes drive.
- [`scripted_render_passes.md`](scripted_render_passes.md) — authoring passes and
  shader libraries in Lua (`.vrp.lua`, `.vshaderlib.lua`).

## Renderer architecture

Rendering is camera-driven. Each `CameraComponent` carries a `rendererKey` string
that selects which registered renderer cooks that camera's view. The runtime
registers a small set of renderers at startup, e.g. in
`source/vultra_app/src/main.cpp`:

```cpp
renderService->registerRenderer(createRef<UniversalRenderer>());      // "universal"
renderService->registerRenderer(createRef<UniversalRtRenderer>());    // "universal_rt"
renderService->registerRenderer(
    createRef<DeclarativeRenderer>("builtin://render/ui_editor.vrg.json", "editor-ui2d"));
```

Most renderers are thin wrappers around `DeclarativeRenderer`
(`source/vultra/src/function/rendering/srp/declarative_renderer.cpp`). A
`DeclarativeRenderer` is constructed with a graph URI plus a renderer key, e.g.:

```cpp
m_GraphRenderer = createScope<DeclarativeRenderer>(graphUri, "universal");
```

At build time it:

1. Loads and parses the `.vrg.json` render graph.
2. Resolves each graph node into a concrete **builtin pass adapter** (matched by the
   node's `type`), wiring node `inputs` to upstream node outputs.
3. Cooks the resolved passes into a **frame graph**, declaring the GPU resources
   (textures/buffers) each pass reads and writes and letting the frame graph schedule,
   alias, and barrier them.

So the `.vrg.json` is the *authored* graph; the frame graph is the *resolved,
scheduled* graph for one frame. The actual mesh submission, culling, and GBuffer
filling inside those passes is the GPU-driven pipeline — see
[`gpu_driven_pipeline.md`](gpu_driven_pipeline.md). Keep this page at the graph level.

## Renderer tiers

There are three explicit builtin render-graph tiers under `builtin/render/`:

| File | Tier | Notes |
| --- | --- | --- |
| `universal.vrg.json` | High-end | Full deferred path: depth pre-pass, GBuffer, shadows, SSAO/SSR, deferred lighting, bloom, tone mapping, FXAA, post effects. A `universal_highend` rename is planned but **not applied yet** — the file and key are still `universal`. |
| `universal_compat.vrg.json` | Compatibility | Minimal forward-ish path (`CompatibilityBaseColor`) for limited backends (e.g. WebGPU, Android). |
| `universal_rt.vrg.json` | Ray tracing | `RayTracingPrimary` + tone mapping + UI overlay. |

### How a tier is selected

The `UniversalRenderer` (`source/vultra/src/function/rendering/srp/builtin/universal_renderer.cpp`)
picks between the high-end and compatibility graphs in `init()`:

```cpp
const bool useCompatibilityFeature =
    kForceCompatibilityFeature                       // true on Android
    || forceCompatibilityByCli                       // --render-profile=compat
    || backendApi == rhi::RenderBackendApi::eWebGPU; // WebGPU backend

const char* graphUri = useCompatibilityFeature ? "builtin://render/universal_compat.vrg.json"
                                               : "builtin://render/universal.vrg.json";
```

- **`rendererKey` (per camera)** selects *which renderer*: `"universal"`,
  `"universal_rt"`, `"editor-ui2d"`, etc. A camera with no key defaults to
  `"universal"`.
- **`--render-profile=<token>`** forces a tier on the `universal` renderer. Accepted
  tokens are `default | universal | compat | compatibility` (see
  `parseRenderProfileToken` in `demo_app_host.cpp` and the `--render-profile` argument
  in `source/vultra_app/src/launch_options.cpp`). `compat`/`compatibility` map to
  `RenderProfile::eCompatibility`, which forces the compatibility graph.
- Ray tracing is reached through a separate renderer (`universal_rt`), not a profile
  token — set the camera's `rendererKey` to `"universal_rt"`.

## The `.vrg.json` format

A render graph is JSON with three top-level sections: `resources`, `passes`, and an
editor-only `meta` block. Below is a real (trimmed) excerpt from
`builtin/render/universal.vrg.json`:

```json
{
  "resources": [
    { "name": "backbuffer" }
  ],
  "passes": [
    { "enabled": true, "id": "DirectDepthPre", "type": "DirectDepthPre" },
    {
      "enabled": true,
      "id": "DirectGBuffer",
      "type": "DirectGBuffer",
      "inputs": { "depth": "DirectDepthPre.depth" }
    },
    {
      "enabled": true,
      "id": "DeferredLighting",
      "type": "DeferredLighting",
      "inputs": {
        "color":  "DirectGBuffer.color",
        "depth":  "DirectGBuffer.depth",
        "normal": "DirectGBuffer.normal",
        "shadowMap": "ShadowMap.shadowMap"
      },
      "params": { "ambientIntensity": 1.0, "shadowStrength": 0.85 }
    },
    {
      "enabled": true,
      "id": "FinalComposition",
      "type": "FinalComposition",
      "inputs":  { "source": "UiOverlay.color" },
      "outputs": { "target": "backbuffer" }
    }
  ],
  "version": 3
}
```

Fields:

- **`version`** — graph schema version (currently `3`).
- **`resources`** — externally named resources the graph imports/exports. The
  `backbuffer` is the swapchain target the final pass writes into.
- **`passes`** — the node list. Each node has:
  - `id` — unique node instance name (used to reference its outputs).
  - `type` — the builtin (or scripted) pass adapter to instantiate. The
    `DeclarativeRenderer` matches `type` to a registered pass.
  - `enabled` — whether the node is cooked into the frame graph.
  - `inputs` — a map of `slotName -> "<NodeId>.<output>"`. Each value references the
    output slot of another node, forming the dependency edges (e.g.
    `"DirectGBuffer.depth"` reads the `depth` output of the `DirectGBuffer` node).
  - `outputs` — optional map binding a node output to a named graph resource (only
    `FinalComposition` binds `target` to `backbuffer` above).
  - `params` — per-pass tunables (booleans, numbers, enums). These are the same
    settings exposed in the renderer UI panels.
- **`meta.editor.nodes`** — editor-only canvas layout (`pos` per node). It has no
  effect on rendering and is regenerated by the editor.

The ray-tracing graph additionally uses a `viewMode` field per node (`"inherit"`),
and the compatibility graph keeps only a handful of nodes — both are good minimal
references.

## Project file types

A project can contribute four render-related file types:

| Extension | Purpose | See |
| --- | --- | --- |
| `.vrg.json` | Declarative render graph (nodes, resources, params, editor layout). | this doc |
| `.vrp.lua` | Lua-authored render pipeline / pass definitions (`setup`/`execute`). | [`scripted_render_passes.md`](scripted_render_passes.md) |
| `.vshaderlib.lua` | Project shader-library declarations and shader globs. | [`scripted_render_passes.md`](scripted_render_passes.md) |
| `.vmatgraph.json` | Material graphs compiled to generated shader sources. | [`material_custom_nodes.md`](material_custom_nodes.md) |

Projects ship their own graph (e.g. `res://render/default.vrg.json`) which can be hot
reloaded and is selected the same way builtin graphs are.

## The Render Graph editor

The editor provides a dedicated Render Graph window
(`source/vultra_app/src/editor_app/ui/windows/render_graph_window.cpp`) built on
`imnodes`. It offers:

- **Live runtime preview** — a 16:9 preview of the graph rendering the current scene
  while you edit, so parameter changes are visible immediately.
- **Runtime graph inspection** — it reads back the resolved runtime frame graph
  (nodes, edges, kinds) so you can see what the declarative graph actually cooked into.
- **Resource thumbnails** — small previews of frame-graph textures (GBuffer targets,
  intermediate render targets) are cached and shown per resource.
- **Node placement & wiring** — nodes can be placed on the canvas, connected slot to
  slot, and their `params` edited inline; the layout is persisted into
  `meta.editor.nodes`.

Edits are saved back to the `.vrg.json`, and the pipeline can be reloaded in place via
`renderService->reloadRenderPipeline(uri, rendererKey)` (also exposed through the
runtime MCP `reload_pipeline` tool).

## Extending the renderer

Projects extend or replace rendering in two complementary ways:

1. **Declaratively** — edit the `.vrg.json`: toggle `enabled`, retune `params`,
   re-wire `inputs`, or swap a node's `type` for a different pass. Because the graph is
   data, no recompilation is needed.
2. **Via Lua passes** — author custom `setup`/`execute` passes in `.vrp.lua` and
   reference them from a graph node's `type`, with shader libraries declared in
   `.vshaderlib.lua`. See [`scripted_render_passes.md`](scripted_render_passes.md).

Plugins can also contribute passes and shaders (and upscalers) that projects then wire
into their own graphs — see [`plugins.md`](plugins.md) and
[`render_upscaler_plugins.md`](render_upscaler_plugins.md).
