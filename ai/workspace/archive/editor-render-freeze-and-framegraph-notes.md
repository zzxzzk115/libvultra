# Editor Render Freeze And FrameGraph Notes

Date: 2026-05-31

## Change

- Game View now freezes its render camera when editor simulation is not advancing.
- Scene View's embedded Game View overlay uses the same static-frame policy.
- Playback, unpaused play, and single-step frames still render continuously so
  shader time, scripts, particles, and physics can advance.
- Static preview invalidates on render target size, project generation, asset
  generation, scene content generation, primary camera parameters, and dirty
  state edges.
- Runtime frame graph DOT/JSON snapshots are only generated while frame graph
  texture/debug capture is enabled. Normal rendering no longer builds those
  strings per camera per frame.
- Fixed the runtime profiler scope for `FrameGraph::build`; it no longer
  includes `FrameGraph::execute` as child time.
- `FrameGraph::execute` CPU cost was not caused by pass lambda copies. The fg
  package stores pass callbacks once and invokes them during execution.
- Reduced direct depth, direct gbuffer, and shadow pass CPU submission overhead
  by avoiding repeated descriptor rebuilds for stable descriptor sets on every
  draw. Stable sets are now rebound only when the active pipeline changes; the
  per-draw uniform-buffer descriptor still updates per draw.
- Scene View and Game View now check the active docking tab before submitting
  render cameras. Inactive dock tabs keep their render targets alive but no
  longer render in the background.
- Static editor previews can override per-camera frame time. Frozen Game View,
  Scene View's embedded Game View, asset thumbnails, Inspector model preview,
  render with `time=0` and `deltaTime=0` so time-dependent shader output is
  deterministic. Material Graph preview uses a dedicated preview time slider
  with a play/pause toggle so shader `time` nodes can be authored and inspected
  deterministically.
- Editor Play Mode owns an independent game clock. Game View and Scene View's
  embedded Game View always use that clock: it starts at 0 on Play, pauses
  without advancing, steps by 1/60, and resets to 0 on Stop.
- Game View uses a separate override RenderWorld cooked from the same world
  with the editor game clock. Scene View keeps the normal main-world cook and
  global/editor time, so Scene View time nodes continue to animate without
  affecting Game View time.
- Game View resize invalidates its frozen-frame signature when resize is
  requested, when a new render target is created, and when a pending target is
  promoted. This forces a redraw after resize instead of waiting for focus.
- Editor shutdown now resets render scene state, and released override
  RenderWorld slots are pruned immediately on the next render cleanup pass.
- DirectGBuffer now carries a PBR MR texture parsing mode. The default mode
  keeps glTF metallic-roughness semantics (B=metallic, G=roughness); FBX
  `Specular` fallback mode handles ORCA/SunTemple packed textures by using
  G=roughness and B=metalness. The R channel is not used as material AO for
  this fallback because several SunTemple `*_Specular.dds` files store it as
  all zero, which would black out ambient/IBL. The vasset importer treats
  matching `*_Specular` textures as PBR MR hints so SunTemple does not fall
  back to Phong.
- BC5/ATI2 normal maps are normalized at import time instead of detected in the
  shader. `*_Normal.dds` style assets are baked to RGBA8 KTX2 with Z
  reconstructed, and directories whose README declares `Normal (DirectX)` bake
  the tangent-space Y flip into the imported texture. DirectGBuffer can then
  sample a generic RGB normal map without asset-specific shader branches. The
  baked KTX2 payload is then BasisU-compressed so 2048x2048 SunTemple normals
  are about 4.2 MB instead of an uncompressed 16.8 MB.
- Directional shadow maps render with no face culling. This avoids dropping
  FBX/UE meshes whose imported winding or transform basis differs from the
  engine's usual front-face assumptions.
- GBuffer and high-end mesh shaders now transform tangents with the normal
  matrix and fold the model determinant sign into tangent handedness. This keeps
  normal-map TBN frames correct for FBX assets with non-uniform or mirrored
  transforms. The raytracing primary hit path uses the same rule.

## Verification

- `git diff --check`
- `git -C external/vasset diff --check`
- `xmake build -y vultra-app`
- `xmake build -y test-importers`
- `build\windows\x64\release\vultra-app\vultra.exe asset import build\test2\resources --reimport`

## Follow-up

- True compiled FrameGraph reuse is still open. The next step is to split
  immutable graph topology from per-frame imported resources and camera data.
- CPU-driven default/universal renderers still need frustum culling and draw
  sorting/batching for large scenes such as SunTemple.
