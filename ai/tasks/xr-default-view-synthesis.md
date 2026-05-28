# XR Render Graph Stereo and View Synthesis Redesign

Date: 2026-05-28

## Goal

Remove the graph-facing `XrViewSynthesis` wrapper pass and make XR render graph
composition explicit only where view synthesis needs left/right eye targets.

## Scope

- Remove `XrViewSynthesis` from the runtime render graph registry.
- Remove `XrViewSynthesis` from the Render Graph editor pass catalog.
- Keep normal graphs implicit: `FinalComposition` without an output selector
  writes to the current render target, including XR stereo/multiview targets.
- Add final-composition output selection for `left_backbuffer` and
  `right_backbuffer` through schema v0.3 `ResourceRef` selectors.
- Update `default_xr.vrg.json` so it no longer contains synthesis.
- Add a reserved `xr_view_synthesis.vrg.json` graph that documents explicit
  left/right final composition outputs for future atomic passes. Its placeholder
  passes stay disabled until producers exist.

## Acceptance

- `xmake build -y vultra-app` passes.
- `resources/render/default.vrg.json`, `default_xr.vrg.json`, and
  `xr_view_synthesis.vrg.json` parse as JSON.
- Loading an old graph that references `XrViewSynthesis` reports an unknown
  pass type instead of silently executing a disabled wrapper.
- XR normal graph rendering remains multiview-first.

## Follow-Up

- Add `XrGeometryWarp` as a non-adaptive atomic pass.
- Add `XrDepthAwarePullPush` as an atomic repair pass.
- Keep adaptive geometry warping out of the initial atomic implementation.
