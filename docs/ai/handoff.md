# Vultra Handoff

## Workspace

Branch `dev-VRI` contains the composition/ECS/example migration. Cleanup and publication are authorized.
Preserve recovery autostash `884598d3`; its deletion was previously rejected by automatic approval.
No external PVW repository/remote work is authorized.

## Current state

- One selected-feature core, direct VRI/RenderGraph and optional authoritative EnTT world. RuntimeContext owns
  desktop services; callers own servers/catalogs. Research, editor/tool policy and player delivery are separate.
- Native/Lua/C# domains share world lifecycle/identity and generated properties/C ABI. Every file/ABI version is
  `1`; external modules need matching SDK rebuilds. The player preset excludes research/editor, debug UI and VMs.
- Example groups/modes declare source/package requirements through `libvultra_examples`. Existing modes remain;
  `ui.native` adds the ABI plugin. `example-offline` covers explicit GPU ownership and C++/JSON compute parity.
- Human docs are README, BUILD.md, docs/get_started.md, docs/architecture.md and docs/roadmap.md. Former guides were
  consolidated and removed. Architecture diagrams cover dependencies, frame lifecycle, ECS/render data and bindgen;
  remaining engine/research/shader gates live in the roadmap.

## Verification and continuation

Prior Linux release/SDL3 builds pass with XR enabled/disabled. Composition, copied no-xmake tools, SDK/VPK,
Python/CLI HDR parity and failure/recovery evidence is under `build/.tmp/{composition,c3,c3-scripting,c4}/`
and isolated `modules-*`/`tools-*`. Final example evidence is in `build/.tmp/example-composition/` and
`examples-uasv12fy`: 18 selections, three embedded profiles, eight serial CPU/offscreen GPU/UI/script regressions,
13 generator checks/tests, formatting/tidy and zero clangd diagnostics across all example TUs.
Root all/SDL3/OpenXR configuration/database is restored. Documentation links and Mermaid rendering are checked.
Final publication evidence is in `build/.tmp/cleanup-publish-afHJ9CyZ/`: full build and all 52 serial tests pass
on a private authenticated Xvfb/Openbox display, with Vulkan/synchronization validation and no GPU errors.
All 230 changed C/C++ files pass formatting; 17 core implementation TUs pass clang-tidy; generated API check and
13 generator tests pass. Offline C++/JSON images are byte-identical; gain, gamma, alpha and failure/recovery checks pass.
Getting-started snippets build/link independently with desktop, GPU/image I/O and Graph/image I/O selections;
format/tidy and source-boundary checks pass in `build/.tmp/get-started-kdwajf4h/`.
GPU evidence is in `build/.tmp/get-started-gpu-hneh287u/`: NVIDIA RTX 4060 Laptop, driver 615.71.09;
unmodified offline/graph programs and a desktop 60-frame/onPostRender-readback variant produce identical
640×360 RGBA pixels. Desktop used private Xvfb/Openbox; final Vulkan/synchronization validation logs are clean.

Physical-desktop interaction/player frames, Windows/D3D12 and physical-headset acceptance remain unverified.
Historical Windows checks predate this migration; existing Slang E41012 remains. Local Monado simulation used
[25.1.0](https://gitlab.freedesktop.org/monado/monado/-/tree/v25.1.0) with upstream
[MR 3022](https://gitlab.freedesktop.org/monado/monado/-/merge_requests/3022),
under [BSL-1.0](https://gitlab.freedesktop.org/monado/monado/-/blob/v25.1.0/LICENSE); this is not current headset validation.

Continue from roadmap's E0 → E1/E2 → E3 acceptance: common script exports/scene attachments, Resource/component
metadata and script-reference reload, then signals/input routing, undo, PackedScene and independent Play.
Research/editor ABI convergence and remaining graphics/platform gates stay explicit. Advanced servers, parallel
jobs, GPU-driven rendering and vendor passes are optional later work. Do not weaken diagnostics or assertions.
