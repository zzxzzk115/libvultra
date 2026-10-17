# Vultra Handoff

## Workspace

Branch `dev-VRI` contains the composition/ECS/example migration and the E0 Resource slice described below.
Preserve recovery autostash `884598d3`; its deletion was previously rejected by automatic approval.

## Current state

- Resource metadata/codecs share the Node property catalog and generated scene ABI. SceneTree owns resources;
  material lists are borrowed views. Native/Lua/safe C# use the same identities and semantic setters.
  Inspector edits pass through world/project reference validation.
- Version-1 scenes use `resources: [{id, type, data}]`, with forward references resolved after construction.
  The old material-only format is removed. Runtime ObjectIds are regenerated; persistent AssetIds survive reload.
  Custom resources round-trip through the world's catalog, including default `save()`.
- Tone mapping clears its write-only output. Depth-read barriers include pinned VRI's attachment STORE while
  graph dataflow stays read-only for correct pass culling. Tests reject VRI errors from stdout and stderr,
  save the complete diagnostic and fail even when the executable exits successfully.
- Feature composition remains additive: minimum GPU and CPU world builds need no renderer/editor/language host.
  Public contracts live in architecture.md; outstanding engine, research and delivery gates live in roadmap.md.
  Every file/ABI version remains `1`; external modules must rebuild against matching generated headers.

## Verification and continuation

Current evidence is under `build/.tmp/e0-resources-76UotuG2/`: full Linux release/SDL3/OpenXR build and all 53
serial tests pass on a private authenticated Xvfb/Openbox display, with clean Vulkan/synchronization diagnostics.
Offline Python/CLI HDR parity, failure/recovery and copied-library/VPK checks pass without desktop or xmake.
Generated API consistency, 14 generator tests, formatting on 38 changed C/C++ files and clang-tidy on all 22
changed C++ translation units pass. Inspector and scene readback images were also visually inspected.
Resource tests cover custom codecs, forward references, rejected-save recovery, semantic writes, wrong/foreign/
retired identities and expired callback borrows. Native/Lua/C# lifecycle fixtures exercise resource creation,
property edits and removal. Inspector tests capture the edited resource panel.
The isolated minimum GPU probe selects 14 sources; the CPU world probe selects 39 without GPU/window packages.
Root all-features/SDL3/OpenXR configuration remains selected.

Physical-desktop interaction/player frames, Windows/D3D12 and physical-headset acceptance remain unverified.
Existing Slang E41012 remains. Offline/private-X11 evidence does not establish Wayland, clean-machine or headset
acceptance. Prefer offline QA; private Xvfb/Openbox tests must not alter the desktop display or its sockets.

Continue E0 with component/script-class metadata, script-facing schema discovery and common exports, then the
roadmap's E1/E2 → E3 acceptance slice. This Resource work does not complete E0 or the full engine/research roadmap.
