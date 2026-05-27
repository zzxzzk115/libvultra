# XR Default View Synthesis

Date: 2026-05-27

## Goal

Add a `default_xr.vrg.json` render graph and built-in extension points for
stereo view synthesis.

## Scope

- Add graph-facing `XrViewSynthesis` pass.
- Add graph parameters for future warping and inpainting backend selection.
- Add default backend names as graph-facing configuration values:
  - `adaptive_mesh_graphics`
  - `pull_push`
- Add a default XR graph that routes the existing default renderer through the
  disabled synthesis stage.
- Keep the initial C++ pass as a source-forwarding stub until the real backend
  can be implemented without device-lost failures.
- Keep unrelated project scene changes untouched.

## Acceptance

- `xmake build -y vultra-app` passes.
- `resources/render/default_xr.vrg.json` parses as schema v0.3.
- Existing `default.vrg.json` remains usable for non-XR rendering.
- The design supports future replacement of warping/inpainting backends without
  changing graph structure.
- `XrViewSynthesis` remains disabled by default until a stable implementation is
  restored.
