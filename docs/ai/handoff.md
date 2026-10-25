# Handoff

## Start here

Continue on `dev-VRI`. Clone that branch to obtain the library source, checked-in generated API and this handoff:

```sh
git clone --branch dev-VRI https://github.com/zzxzzk115/libvultra.git
cd libvultra
xmake f -m release -y
xmake build -y --all
xmake run example-offline --frames 3 --output build/.tmp/first-run
```

Configure again on the destination machine; do not copy `.xmake`, compilation databases or binaries.

Read [README](../../README.md), [BUILD](../../BUILD.md), [architecture](../architecture.md) and
[roadmap](../roadmap.md). They describe supported behavior and the remaining design gates.

## Repository boundaries

- libvultra owns graphics/platform services, RenderGraph, asset data, generic properties/C ABI and optional UI.
  Its examples are `example-basics`, `example-offline` and `example-ui`.
- Vultra owns the EnTT world, scene/resources, project/export semantics and Native/Lua/C# hosts.
- VultraLab owns experiments, reference rendering, research examples, batch and research passes.
- VultraPy owns the standalone Python/NumPy compute frontend.

Consumers pin `external/libvultra` to the corresponding library commit. Use `git clone --recurse-submodules`
or `git submodule update --init --recursive`; their default builds need no sibling checkout. The optional
`--libvultra_dir=../libvultra` override is for coordinated development. Keep code and dependent pins synchronized.
Each product has its own handoff; do not restore engine/research sources here.

## Last implementation

RenderGraph now distinguishes content producers from execution hazards. An explicit `discardPrevious` marks a
full overwrite; preservation remains the default, including partial copies. Side-effect pass definitions remain
live, history retains its final observable writer, and graph definitions prune unreachable instances before GPU
factory creation. Invalid metadata/cycles and live capability failures still report errors.

The partial-copy regression exposed a VRI Vulkan bug. Keep `external/vri/texture_copy_region.patch` with the other
seven pinned patches. The package is VRI 0.2.0 at `d0c78964cc50788eb49d4b7daf967ef3222d2530`; the sibling VRI
checkout is not used by this build. It remains on the older, clean `dev/neural-shader` branch.

The cleanup removed superseded engine/script/research copies after checking their preserved counterparts.
Library feature/example/tool checks now select only library targets. No rendering algorithm, dependency version,
image tolerance or public format version changed. The historical editor and script integration tests remain
with their owning engine migration, rather than pretending to run as library tests.

## Verification

Before cleanup, the graph slice passed three affected library tests, six Lab tests, engine GPU scene-sync,
minimal `graph`/`graph_json` builds, generation checks and scoped clang-tidy. Finite C++/JSON offline PNG/PFM
outputs were byte-identical and visually inspected. Logs/images are in `build/.tmp/render-graph-core` on the
original machine; they are local evidence, not source dependencies.

Cleanup checks passed: full library/engine builds, five library tests, ten engine tests, six Lab tests, six Python
tests, 45 generator tests, four module closures and three copied cooking tools. The offline C++/JSON images still
match the preceding hashes. Lab's GPU fixture now reads its own shaders instead of the retired library path.
Modified C++ passes formatting and scoped clang-tidy; library/engine generation is current.
Repeat commands are in BUILD.md. Evidence directories are local diagnostics, not prerequisites for a clone.
Linux Vulkan/offscreen results do not establish Windows, Metal, D3D12, clean-machine
shipping, browser hardware or physical Quest support. Prior Android ARM64/browser probes are not a new port
acceptance. Use offline QA; visible tests require Workspace 5 and must not alter desktop displays/sockets.

## Continue in this order

1. RG1: resource dimensions, mips/layers/samples, buffer layouts and subresource initialization/views/barriers.
2. RG2: declaration/resolve/prepare stages and explicit execution edges, with failed-candidate recovery.
3. RG3: typed parameters and invalidation/history rules; RG4: diagnostics and backend acceptance.
4. Finish common binding IR/safe SDK generation and native retained GUI as separate tested slices.

The graph still handles single-mip/single-layer 2D textures, whole-resource states and numeric parameters.
It does not yet meet Falcor's general composition contract. Do not expand Lab's workbench/pass collection to
hide those gaps. Direct VRI and `addPass()` remain supported.

Other boundaries to preserve: cooked rendering/VGui do not select host compilers/importers; all selected code
links one `vultra` archive; VGui currently allows one live context; EditorGui frame handles are borrowed; generated
C tables have size/signature checks but safe SDK generation is incomplete. All file/ABI versions remain **1**.

## Local recovery notes

The original machine retains recovery autostash `884598d3` and pre-cleanup snapshots under
`build/.tmp/handoff-cleanup/original`. They are not needed to build the committed branch. The earlier transfer
archive is a pre-publication backup; GitHub branches and dependent submodule pins are the continuation source.
The abandoned `VRI-api-0.2` worktree remains a separate uncommitted experiment, not the active API or a build
baseline. VRI source changes are not part of this handoff publication. No release or tag is created.
