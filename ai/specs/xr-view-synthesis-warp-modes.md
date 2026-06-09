# XR View Synthesis — Warp Modes & Enlarged Source (design, not yet implemented)

Date: 2026-06-09
Status: **design only** — the warp/inpaint backends exist and are wired in a demo graph;
the engine-level "render-once + enlarged source + per-mode synthesis" described here is NOT
built yet. Pick this up later.

## Where we are now

Done and verified-in-code:
- `XrGeometryWarp` (geometry-shader, matrix-driven, depth-aware hole detection) and
  `XrPullPushInpaint` (depth-aware pull-push) backends — faithful ports of
  `../pixelwise-viewpoint-warping` `warp.geom`/`push.frag`/`pull.frag`.
- Both depth-aware ON and OFF paths (warp: `useDepthAware` push-constant; pull/push:
  `USE_DEPTH_AWARE` keyword variant). Alpha-validity convention consistent across warp↔inpaint.
- Defaults aligned to the reference: `gridSize=1`, `sideLenThreshold=0.01`,
  `useDepthAware=true`, `depthThreshold=0.01` (= `GraphicsWarpingSettings` + `InpaintingSettings`).
- A runnable graph `resources/render/xr_view_synthesis.vrg.json`:
  scene → `XrGeometryWarp`(left→right) → `XrPullPushInpaint` → `FinalComposition`.

**What that demo graph is NOT:** it renders BOTH eyes via the normal multiview chain and then
warps left→right, *overwriting* the rendered right eye. The reference instead renders **one**
source view (possibly enlarged) and **synthesizes** the other eye(s) — that is the whole point
(render once, warp the rest) and is what gives the perf win and the enlarged-source quality.

Still hardcoded (minor): pull/push neighbour count `INDEX_COUNT=2` (= reference
`InpaintingSettings.gridSize=2`) is a shader macro, not an exposed setting.

## The reference model (what we are matching)

`settings.hpp::VrExtraWarpingSettings` + `renderer.cpp` (`VrWarpMode`):

- `VrWarpMode { None, LeftToRight, RightToLeft, CenterToLeftRight }`.
- `warpingUseLargerImage` + `warpingLargerOffsetScale`: render the SOURCE eye with a widened
  FOV. For LeftToRight, extend the source's `angleRight` toward the other eye
  ([renderer.cpp:717-728](../../../pixelwise-viewpoint-warping/source/common/src/renderer.cpp#L717)):
  ```
  left  = tan(|angleLeft|)  * near
  right = tan(|angleRight|) * near
  newAngleRight = atan( (right + (left + ipd - right) * warpingLargerOffsetScale) / near )
  ```
  `inverseOriginalProjection` keeps the inverse of the ORIGINAL (un-enlarged) projection — used
  to recover the original-FOV view out of the enlarged render (the "warp back", see below).
- `CenterToLeftRight`: build a single CENTER FOV that encloses BOTH eyes (union in tangent space,
  scaled by `centerFovXScale`/`centerFovYScale`), render once, warp to L and R
  ([renderer.cpp:777+](../../../pixelwise-viewpoint-warping/source/common/src/renderer.cpp#L777)).
- Data split: `m_CameraInfos[]` = the SOURCE view(s) actually rendered; `m_WarpInfos[]` = the
  TARGET view(s) to synthesize (each carries the target eye's `view`/`viewProjection`).

## Key insight: every displayed eye is a warp from a source

Including the source eye itself. With an enlarged source, the source eye's OWN display is a
**warp-back / crop**: reproject the enlarged-source render to the original source-eye FOV.
Without enlargement that warp is identity (pass-through). This makes the abstraction uniform:

> A synthesis plan = a set of rendered SOURCE views + a set of displayed TARGET views, where
> each target is produced by warping (reprojecting) from exactly one source. The source eye is
> just a target whose source happens to be itself (identity, or a crop when enlarged).

This generalizes cleanly to the "exotic" operations:
- **LeftToRight (enlarged):** sources `{ L_enlarged }`; targets `{ L ← warp-back/crop from
  L_enlarged, R ← warp from L_enlarged }`.
- **RightToLeft (enlarged):** mirror of the above.
- **CenterToLeftRight:** sources `{ Center_union }`; targets `{ L ← warp, R ← warp }`.
- **Temporal:** sources `{ PrevFrame }`; targets `{ Current ← warp }`.
- **Mono / None:** sources `{ V }`; targets `{ V ← identity }`.
- Future N→M (e.g. 2 sources → quilt of M views) drops out of the same shape.

## Proposed abstraction (libvultra)

Make the synthesis plan first-class, decoupled from "stereo = two eyes at headset FOV".

```
struct XrSynthesisSourceView {
    RenderCamera camera;        // view + (possibly enlarged/union) projection
    rhi::Extent2D imageSize;    // may be larger than the display
    // produced at runtime:
    FrameGraphResource color, depth;
};

struct XrSynthesisTargetView {
    uint32_t     eyeLayer;      // which display layer (0=left,1=right,...)
    uint32_t     sourceIndex;   // which source it warps from
    glm::mat4    targetViewProj;        // target NDC for the warp
    glm::mat4    sourceInvViewProj;     // unproject the source (the enlarged/union one)
    bool         identity;      // true => pass-through (source eye, no enlargement)
};

struct XrSynthesisPlan {
    std::vector<XrSynthesisSourceView> sources;   // rendered once each
    std::vector<XrSynthesisTargetView> targets;   // synthesized
    // mode/enlarge params that produced it, for the UI/debug
};
```

The warp pass already carries `sourceInvViewProj` + `targetViewProj[2]` in its UBO and writes
per-multiview-layer; generalizing it to "for each target layer, unproject its source and project
to its target" is a small shader/UBO change (the matrices simply come from the plan instead of
from `multiviewCameras`). A target flagged `identity` short-circuits to a straight sample.

## Integration plan (the four layers)

1. **Config — `XRViewComponent` / `XRStereoGraphMode`**
   `xr_view_component.hpp`: add modes beyond `eSingleGraphStereo`:
   `eViewSynthesisLeftToRight`, `eViewSynthesisRightToLeft`, `eViewSynthesisCenterToLR`.
   Add `enlargeSourceScale` (= `warpingLargerOffsetScale`), `centerFovXScale`, `centerFovYScale`,
   `sourceImageScale` (larger image). Inert until the renderer honours them.

2. **Camera cook — camera service**
   Cook the chosen mode + enlarge params from `XRViewComponent` into the `RenderCamera`(s)
   (it already carries `xrFov`, `xrIpd`, `xrEyePosition`). This is where the enlarged/union
   projection and the original-projection inverse are computed (mirror `renderer.cpp`'s math).
   Produce the `XrSynthesisPlan` here (or hand the renderer enough to build it).

3. **View setup — `render_system.cpp`**
   `render_system.cpp` view-setup ([~4924-4953](../../source/vultra/src/function/rendering/render_system.cpp#L4924))
   currently always builds a 2-layer multiview view with both eyes at headset FOV. In synthesis
   mode, instead render the **source view(s)** (often a single mono enlarged/union view), and
   stash the `XrSynthesisPlan` on `RenderView` so the graph/warp can read it. **This file is
   load-bearing for ALL rendering — change it behind the synthesis-mode flag and verify normal +
   XR paths still render.** Do this increment in isolation and test before the rest.

4. **Warp pass — `XrGeometryWarp`**
   Drive the warp from `RenderView`'s `XrSynthesisPlan` rather than `multiviewCameras`: for each
   target layer, set `sourceInvViewProj`/`targetViewProj` from the plan; sample the (possibly
   mono) source; emit per-eye output. The source eye's `identity`/crop target is just another
   entry. Pull-push then repairs each synthesized target as today.

Suggested order (each testable): 1+2 (inert config + cook) → 4 warp reads a plan but plan still
== current stereo (no behaviour change) → 3 render-once enlarged source (visible: right-eye edge
holes shrink) → CenterToLeftRight → warp-back/crop polish for the source eye.

## Notes / open questions

- **Warp-back for the source eye:** with an enlarged source, the source eye must be cropped/warped
  back to its original FOV for display (use the original-projection inverse). Decide whether to do
  it as an `identity`-ish warp target or a dedicated crop sample.
- **Mono source → stereo output:** the warp's `USE_MULTIVIEW` path assumes a multiview (array)
  source. A single enlarged source is one layer; either sample layer 0 for all targets, or render
  the source into a 1-layer texture and special-case the sampler.
- **Center union FOV:** compute in tangent space over both eyes' `angleLeft/Right/Up/Down`
  ([renderer.cpp:800-814](../../../pixelwise-viewpoint-warping/source/common/src/renderer.cpp#L800)),
  apply `centerFovXScale/Y`.
- **Larger source image:** `warpingUseLargerImage` may also raise the source resolution
  (`centerImageSize`) for quality; the source render target then differs from the display extent.
- **Backends stay pluggable:** all of the above feeds the same `XrGeometryWarp`/`XrPullPushInpaint`
  (or a Lua backend) — the plan changes *what* is rendered and *which matrices* the warp gets, not
  the warp/inpaint contract.

## Reference & current files

- Reference: `../pixelwise-viewpoint-warping/source/common/include/settings.hpp`
  (`VrExtraWarpingSettings`, `VrWarpMode`, `GraphicsWarpingSettings`, `InpaintingSettings`);
  `.../src/renderer.cpp` (the `VrWarpMode` switch, FOV math, `m_CameraInfos`/`m_WarpInfos`).
- Current: `source/vultra/.../builtin/passes/xr_view_synthesis_pass.{hpp,cpp}`;
  `builtin/shaders/passes/general/xr_view_synthesis_geometry_warp.{vert,geom,frag}.vshader`,
  `..._pull.frag.vshader`, `..._push.frag.vshader`;
  `source/vultra/.../render_system.cpp` (view setup); `xr_view_component.hpp`;
  `resources/render/xr_view_synthesis.vrg.json`.
