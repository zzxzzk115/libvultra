# Scripted Render Passes (Lua `setup` + `execute`)

The declarative renderer supports two kinds of project-authored render-graph
passes:

| Kind | Lua surface | Execute body |
| --- | --- | --- |
| **Declarative** (`RenderGraphPass` with a `shader` table) | declares shader + I/O only | engine-fixed (`FullscreenPassRuntime` / `ComputePassRuntime`) |
| **Scripted** (`RenderGraphPass` with `setup` + `execute`) | drives the FrameGraph builder and command recorder directly | **authored in Lua** |

A scripted pass is a full SRP extension point: Lua declares FrameGraph inputs and
outputs, creates transient targets, reads engine/blackboard resources, queries
the GPU scene, selects shaders from a shader library **by name**, and records the
draw or dispatch itself.

## Authoring

A scripted pass file lives anywhere under the project (e.g.
`resources/render/passes/*.lua`) and `return`s a `RenderGraphPass` table that
contains `setup` and `execute` functions:

```lua
local state = {}   -- shared upvalue: carries values from setup to execute

return RenderGraphPass {
    type    = "MyPass",
    inputs  = { "source" },
    outputs = { "color" },
    params  = {
        { name = "strength", type = "float", default = 0.5 },
    },

    setup = function(ctx)
        local src = ctx:getInput("source")
        ctx:read(src, { set = 3, binding = 0, stage = "fragment" })
        local out = ctx:createColorTexture { name = "MyPass Color", inherit = src }
        ctx:writeColor(out)
        ctx:setOutput("color", out)
        ctx:useGraphicsShader {
            vertexLibrary = "builtin", vertex = "fullscreen_triangle.vert",
            fragmentLibrary = "project", fragment = "my_pass.frag",
        }
        state.strength = ctx:paramFloat("strength", 0.5)
    end,

    execute = function(rc)
        if not rc:bindPipeline() then return end
        rc:bindDescriptorSets()
        rc:pushConstants("fragment", { strength = state.strength })
        rc:beginRendering(); rc:drawFullscreen(); rc:endRendering()
    end,
}
```

Then reference it from a render graph (`.vrg.json`) by `type`:

```json
{ "id": "MyPass", "type": "MyPass", "inputs": { "source": "SsrComposite.color" } }
```

See [`resources/render/passes/scripted_pixelate.lua`](../resources/render/passes/scripted_pixelate.lua)
for a complete working example that reuses the existing `pixelate.frag` shader.

## Node params from shader reflection

A pass's shader `[properties]` (name, type, default, `range(...)`, `enum(...)`) are
**auto-exposed as render-graph node params**, so the editor shows them and writes
overrides into the `.vrg.json`; `pushConstants` then reads them back by name.

- **Declarative project passes** (with a `shader` table): reflected automatically
  from the fragment/compute shader.
- **Scripted passes**: add an optional `shader = { fragmentLibrary=, fragment= }`
  (or `compute=`) hint at the top level. The real shader is still selected in
  `setup`; the hint exists only to drive reflection. An explicit `params = {...}`
  block overrides or extends the reflected set.

## `setup(ctx)` API — FrameGraph build context

`setup` runs during graph build. The `ctx` object is only valid for the duration
of the call; do not stash FrameGraph handles in globals (handles are generation
-tagged and a stale handle raises a Lua error).

| Method | Description |
| --- | --- |
| `ctx:getInput(slot)` → handle | resolve a graph input wired in the `.vrg.json` |
| `ctx:setOutput(slot, handle)` | publish a graph output |
| `ctx:getResource(name)` → handle\|nil | read a published engine resource by name (e.g. `"GBufferColor"`, `"DepthTexture"`) |
| `ctx:setResource(name, handle)` | publish an engine resource by name |
| `ctx:createColorTexture { name=, format=, inherit=, storage= }` → handle | allocate a transient target (`format`: `rgba16f`/`rgba8`/`rgba32f`; `inherit` copies a handle's descriptor; `storage=true` adds storage usage for compute writes) |
| `ctx:read(handle, { set=, binding=, stage=, depth= })` | declare a sampled read (`stage`: `fragment`/`compute`) |
| `ctx:writeColor(handle [, index [, clear]])` | declare a color attachment write |
| `ctx:writeStorage(handle, { set=, binding=, stage= })` | declare a storage-image write |
| `ctx:useGraphicsShader { vertexLibrary=, vertex=, fragmentLibrary=, fragment= }` | select graphics shaders |
| `ctx:useComputeShader { library=, compute= }` | select a compute shader |
| `ctx:paramFloat/Int/Bool/String(name, default)` | read a graph param (declared in `params`) |
| `ctx:sceneDrawCount()` / `ctx:hasGaussianSplats()` / `ctx:isGpuDriven()` | query the GPU scene |

## `execute(rc)` API — command recorder

`execute` runs during command recording.

| Method | Description |
| --- | --- |
| `rc:bindPipeline()` → bool | build/bind the pipeline for the shaders selected in setup; returns `false` if unavailable |
| `rc:bindDescriptorSets()` | bind the descriptor sets the FrameGraph prepared from the declared reads/writes |
| `rc:pushConstants(stage, { name = value, ... })` | pack push constants by shader-reflected parameter name (`stage`: `fragment`/`compute`/`vertex`) |
| `rc:beginRendering()` / `rc:drawFullscreen()` / `rc:endRendering()` | record a fullscreen-triangle draw |
| `rc:dispatch(x, y, z)` | record a compute dispatch |
| `rc:dispatchByOutputSize()` | dispatch one workgroup per output texel block (uses the bound compute pipeline's local size and the last `createColorTexture` extent) |

## Notes & limitations (v1)

- **Threading:** scripted `execute` runs on the render-script thread (the thread
  that loaded the pass). A debug assertion enforces this invariant.
- **Errors:** `setup`/`execute` are called as protected functions. A setup error
  leaves the pass with no recorded work; an execute error skips recording. Both
  are logged once per pass.
- **Graphics pipeline state** is currently fixed to the fullscreen-triangle
  configuration (no depth/blend/cull). Custom geometry draws (meshlet/GPU-scene
  indirect) and FrameGraph **buffer** I/O are planned follow-ups; compute passes
  already cover arbitrary buffer work via storage images and dispatch.
