---
name: vultra-builtin-render-pass
description: Use when adding, changing, or reviewing a builtin (C++ engine-side) render pass in libvultra — pass classes under srp/builtin/passes, the RenderPass/ComputePass base, FrameGraph I/O, builtin shaders, RenderFeature wiring, the makeBuiltinFeature dispatch, and resource keys. For Lua/project render-graph passes use vultra-lua-render-pass instead.
---

# Vultra Builtin Render Pass Workflow

A builtin render pass is a C++ class that records work into the FrameGraph. A pass
never runs by itself: it is owned by a `RenderFeature`, the feature is created by a
string id in `makeBuiltinFeature`, and a Lua render pipeline asset lists that feature
id. Adding a pass means touching all four layers, not just the pass class.

## Read First

- A pass is the unit of FrameGraph work; a feature is the unit of pipeline
  composition. Group passes into a feature by lifecycle, not one feature per pass —
  reuse an existing feature when the new pass belongs to the same stage
  (e.g. add a postprocess pass to `BuiltinScreenSpaceFeature`).
- Inter-pass data flows through `ctx.data` keyed by `FrameGraphResourceKey`
  (`resource_keys.hpp`), never through pass member pointers. Read a key with
  `ctx.data.get(...)`, publish with `ctx.data.set(...)`, probe with `contains`.
- Pick the shader profile deliberately: `eGeneral` for fullscreen/postprocess,
  `eHighend` for geometry/lighting, `eCompatibility` for the fallback path. The
  profile selects which `builtin/shaders/passes/<profile>/` directory is searched.
- Do not hardcode a new pass into the renderer's frame loop. Wiring is data-driven
  through the feature id; the Lua pipeline asset decides ordering.

## Registration Chain

1. **Pass class** — add the header/source pair under
   `source/vultra/include/vultra/function/rendering/srp/builtin/passes/<name>_pass.hpp`
   and the matching `.cpp` under `.../src/function/rendering/srp/builtin/passes/`.
   - Inherit `rhi::RenderPass<MyPass>` (graphics) or `rhi::ComputePass<MyPass>`
     (compute); both come from `core/rhi/base_pass.hpp`. Add `friend class BasePass;`.
   - Constructor sets the shader profile: `setShaderProfile(rhi::ShaderProfile::eGeneral)`.
   - Expose `addPass(FrameGraphBuildContext& ctx, ...)`. Return the output
     `FrameGraphResource` when the caller chains it; return `void` and publish via
     `ctx.data.set(key, ...)` when other features consume it by key.
   - Implement `createPipeline(...) const` returning a `GraphicsPipeline` /
     `ComputePipeline`; the base caches pipelines via `getPipeline(args...)`.
2. **Shaders** — author the shader(s) under
   `builtin/shaders/passes/<profile>/<name>.<stage>.vshader`
   (`general` / `highend` / `compatibility`). Load them in `createPipeline` with the
   profile helper: `loadGeneralShader("<name>.frag", vshadersystem::ShaderStage::eFrag)`
   (`loadHighendShader`, `loadCompatibilityShader`, or generic `loadShader`). Pass
   keyword values for variants; compute passes use `computeShaderVariantHash`.
3. **Resource keys** — if the pass output is consumed by another feature, add a key in
   `source/vultra/include/vultra/function/rendering/srp/builtin/resource_keys.hpp`
   (`constexpr FrameGraphResourceKey kResKey_<Name> {.id = vbase::hashLiteral("<Name>")};`).
4. **Feature** — own the pass in a `RenderFeature`
   (`srp/builtin/features/<name>_feature.hpp/.cpp`):
   - Inherit `RenderFeature`, declare `DEFINE_RENDER_FEATURE(MyFeature);`, override
     `void addPasses(FrameGraphBuildContext& ctx)`.
   - Hold passes as `std::unique_ptr<MyPass>`, construct them in the feature ctor,
     and call `m_MyPass->addPass(ctx, ...)` in `addPasses`, gated on the inputs the
     pass needs (`ctx.data.contains(...)`).
5. **Dispatch id** — register the feature in `makeBuiltinFeature(std::string_view id, ...)`
   in `source/vultra/src/function/rendering/srp/declarative_renderer.cpp`. Add the
   header include and a normalized id branch:
   `if (normalized == "my_feature") return std::make_unique<MyFeature>();`
   (pass `*renderService` to the ctor when the feature needs it). Keep the existing
   id aliases; do not rename them.
6. **Pipeline asset** — reference the feature id from a render pipeline so it actually
   runs. Builtin pipelines live under `resources/render/` / `builtin/` as
   `RenderPipelineAsset` Lua that lists `feature.builtin = "my_feature"` entries in
   render order.

## FrameGraph I/O Shape

Inside `addPass`, declare a local `PassData` and call
`ctx.fg.addCallbackPass<PassData>("Name", setup, execute)`:

- **setup** `(FrameGraph::Builder&, PassData&)` — `builder.read(src, {...binding...})`,
  `builder.create<framegraph::FrameGraphTexture>("Out", desc)`, then
  `builder.write(out, framegraph::Attachment{...})`.
- **execute** `(const PassData&, FrameGraphPassResources&, void* ctxPtr)` — recover
  the render context, `setRenderDevice`/`setShaderLib`, `getPipeline(...)`,
  `bindPipeline` + `bindDescriptorSets`, then draw/dispatch
  (`drawFullScreenTriangle()` for fullscreen passes).

## Naming

- Class `MyThingPass` / `MyThingFeature`; files `my_thing_pass.*`, `my_thing_feature.*`.
- Feature dispatch ids are snake_case (`builtin_screen_space`); keep stable aliases.
- Shaders `my_thing.<stage>.vshader` in the profile directory.
- Resource keys `kResKey_<PascalName>` hashed from a stable string literal.

## Verification

- Build: `xmake build -y vultra-app`.
- Confirm the feature id resolves in `makeBuiltinFeature` and the pipeline asset that
  references it loads without falling back.
- Use Runtime MCP (`ai/knowledge/mcp-tools.md`) to prove the pass runs:
  `vultra.runtime.framegraph_snapshot` should list the new pass node;
  `vultra.runtime.frame_resources` / `vultra.runtime.dump_frame_textures` should show
  its output texture; `vultra.runtime.reload_pipeline` after editing.
- Watch for missing-shader-blob log spam — author every stage/variant the pass loads.

## Files To Check

- `source/vultra/include/vultra/core/rhi/base_pass.hpp` (RenderPass/ComputePass/ShaderProfile)
- `source/vultra/include/vultra/function/rendering/srp/builtin/passes/**`
- `source/vultra/include/vultra/function/rendering/srp/builtin/features/**`
- `source/vultra/include/vultra/function/rendering/srp/builtin/resource_keys.hpp`
- `source/vultra/src/function/rendering/srp/declarative_renderer.cpp` (`makeBuiltinFeature`)
- `builtin/shaders/passes/{general,highend,compatibility}/**`
- `resources/render/**` and `builtin/**` render pipeline assets

## Pitfalls

- Forgetting the dispatch-id branch: the feature exists but no pipeline can name it.
- Publishing through member pointers instead of `ctx.data`: breaks cross-feature reuse
  and FrameGraph lifetime tracking.
- Wrong profile: the shader lookup searches the wrong directory and silently misses.
- Authoring a pass class but never adding it to a feature's `addPasses`: it never runs.
