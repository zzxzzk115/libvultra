# README Refresh

## Summary

- Rewrote `README.md` around the current VultraEngine layering:
  `vultra` as the static engine library and `vultra-app` as launcher, editor,
  command-line tool host, and standalone runtime shell.
- Promoted current feature areas: Vulkan/WebGPU, OpenXR, SRP/render graph,
  material graph, `vasset`, `vfilesystem`, VPK packaging/loading, Lua scripting,
  and platform targets.
- Updated asset import/pack examples to match the existing script signatures.
- Removed stale `scripts/bootstrap_vasset_cli.*` WASM instructions. The current
  WASM examples build a host `vultra-app` first, then use the `resources.vpk_pack`
  xmake rule to import/pack resources before linking the WebAssembly target.

## Verification

- `git diff --check -- README.md` passed.
- Confirmed referenced showcase paths for OpenXR Gaussian Splatting, ray tracing,
  and mesh shading exist.
- Confirmed no `bootstrap_vasset_cli` references remain in `README.md`.

## Notes

- MCP bootstrap was attempted but the `vultra` server did not complete the
  handshake, so repository guidance was followed through direct file reads.
