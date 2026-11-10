# Handoff

Continue on `dev-VRI`. Build against the pinned VRI package and the checked-in patches in `external/vri`.
Start with [BUILD](../../BUILD.md), [architecture](../architecture.md), and [roadmap](../roadmap.md).
Keep engine, research applications and Python frontends separate; update consumer submodule pins with API changes.

## Verified interfaces

- RenderGraph has checked graph construction, resource views and ranges, snapshots, native heap placement,
  dependent-history invalidation, and caller-owned queue submission and completion. Do not infer physical overlap
  or safe cross-queue aliasing from a single-queue run. The graph does not schedule asynchronous compute implicitly.
- Slang reflection and bindings support cooked programs, typed material blocks, arrays and function graphs.
  Retain accepted layouts and pipelines on rejected reloads. Full material-model/editor integration remains open.
- A completed vertex-position write can publish conservative primitive bounds and a geometry revision. Static
  topology and native allocations are retained. The shared BLAS rebuilds once per revision, each ray policy refreshes
  its TLAS, and the built-in renderer invalidates cached shadows. Existing external geometry caches must refresh.
- Vulkan and Metal AS scratch patches allow completed-use release and later reacquisition. Native recording errors
  propagate through Expected submission; preserve the pinned patch checksums and rebuild static consumers.
- The upstream device selection, DPI, FileWatch, XR pipeline-cache and asset opacity fixes are included. Cached
  shadows use the checked resource preservation flag and stay outside both graph reuse planners.

## Verification and next work

The fixed-topology fixture passes 24 native ray queries and stable AS footprint checks on Metal and Android Vulkan.
Metal, MoltenVK and Android restore the six raster/shadow outputs exactly. This MoltenVK device does not execute
ray queries. Prefer macOS Metal and MoltenVK for primary checks and use Android Vulkan for additional device evidence.
Use BUILD.md for fixture commands; no machine-specific configuration or generated binaries are required in Git.

Continue topology and stable instance authoring in the consumer, plus selective deformation, refit and retained previous
positions where supported. Complete the remaining reference/AOV, provenance, GPU capture and delivery gates; an
isolated fixture is not end-to-end headset or performance acceptance. Use Expected at changed boundaries; legacy
throwing entry points still exist. Keep serialized formats at version 1.
