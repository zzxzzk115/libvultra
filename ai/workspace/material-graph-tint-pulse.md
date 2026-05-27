# Material Graph Tint Pulse

## Goal

Use a default material graph effect that demonstrates time-driven graph nodes
without depending on alpha masking or transparent-object rendering.

## Changes

- Replaced the default dissolve chain with a high-contrast tint pulse chain:
  `Time -> Multiply -> Sine -> Multiply -> Add -> Base Mix`.
- The pulse now mixes two constant colors, cold blue and hot orange, so it is
  visible in the current runtime parameter path without depending on texture
  sampling, Fresnel, alpha masking, or graph-aware GBuffer execution.
- Kept the material `Opaque` so preview behavior is not coupled to the current
  alpha-mask renderer path.
- Updated the generated default material graph shader snapshot.

## Verification

- Current: `python -m json.tool resources/materials/default.vmatgraph.json` passed.
- Current: `xmake build -y test-material-graph` passed.
- Current: `build/windows/x64/release/test-material-graph/test-material-graph.exe` passed.
- Current: `xmake build -y vultra-app` passed.

## Note

The previous dissolve example exposed the renderer's current alpha-mask
limitation more than the material graph itself. A future graph-aware GBuffer path
can bring back UV-dependent dissolve once alpha and transparent material handling
are ready.

The default graph layout is now maintained by the shared graph layout rules in
`ai/workspace/graph-layout-rules.md`.
