# XR View Synthesis Render Graph

Date: 2026-05-27

## Intent

Vultra render graphs need an XR view synthesis stage that can render one eye or
one center view, synthesize another stereo view through image-space warping, and
repair disocclusion holes through configurable inpainting.

The implementation must be research-friendly:

- render graph files select algorithms through readable parameters;
- C++ code exposes extension points for warping and inpainting backends;
- the default backend is replaceable rather than baked into the graph runtime;
- geometry/tessellation shader dependencies are avoided for portability.

## Target Architecture

The graph-facing abstraction is `XrViewSynthesis`.

The intended implementation owns two backend categories:

- `IXrWarpingBackend`: consumes source color/depth and produces a warped image
  plus validity metadata.
- `IXrInpaintingBackend`: consumes the warped image and repairs holes.

The planned default backend pair is:

- `adaptive_mesh_graphics`: compute shader builds an adaptive screen-space mesh,
  replacing geometry/tessellation shader expansion. A normal graphics pass then
  rasterizes the generated mesh.
- `pull_push`: a graphics pull-push pyramid repairs holes using the alpha
  validity/depth convention produced by the warping backend.

## Render Graph Parameters

`XrViewSynthesis` parameters should stay algorithm-oriented:

- `warpingBackend`: default `adaptive_mesh_graphics`.
- `inpaintingBackend`: default `pull_push`.
- `sourceView`: `left`, `right`, or `center`.
- `targetView`: `right`, `left`, or `stereo`.
- `baseGridSize`: coarse image-space grid cell size in pixels.
- `maxSubdivision`: maximum adaptive subdivision level per cell.
- `sideLengthThreshold`: projected triangle stretch threshold.
- `depthThreshold`: depth discontinuity/inpainting threshold.

Backend-specific parameters may be accepted, but the render graph should not
name shader files or pipeline internals.

## Planned Default Backend Notes

The default adaptive mesh backend should follow Didyk-style image-space stereo
view synthesis, but replace geometry/tessellation stages with compute:

1. Read source color/depth and stereo camera matrices.
2. For each coarse screen-space grid cell, evaluate projected corner positions.
3. Subdivide cells when projected edge length or depth discontinuity exceeds
   thresholds.
4. Emit triangle-list vertices into a storage buffer and write a non-indexed
   indirect draw command.
5. Rasterize the generated mesh in a standard graphics pass.
6. Encode valid/invalid/hole information in alpha for inpainting.

This keeps the adaptive grid model while remaining viable on platforms where
geometry shaders are deprecated or unavailable.

## Extension Points

Custom warping backends may implement:

- pixel splatting;
- mesh shader generated warps;
- optical-flow-based warps;
- temporal reprojection;
- stereo warping with history, HiZ, or TAA data.

Custom inpainting backends may implement:

- pull-push;
- depth-aware neighborhood filling;
- ray traced completion;
- neural or external upscaler/inpainting integrations.

The render graph contract stays stable as long as the backend consumes the same
inputs and publishes a final color resource.

## Current Integration Boundary

The first integration only registers the graph-facing `XrViewSynthesis` pass and
parameters. The pass currently forwards its source texture and is disabled in
`default_xr.vrg.json` while the real warping and inpainting backend is rebuilt
after device-lost failures.

`viewMode` in schema v0.3 currently documents graph intent and is evaluated only
for pass-level `when` conditions. Future work should add first-class execution
semantics for mono-center, left-eye, right-eye, stereo, and history resources so
graphs can explicitly express center-to-left/right and left-to-right synthesis.
