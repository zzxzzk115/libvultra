# Optional research extension

This module reuses Vultra's VRI resources, RenderGraph, scene synchronization and raster renderer. It owns
experiment descriptions/sessions, graph reports, reference path tracing, camera tracks, stereo rendering,
headset/configuration records, FLIP assessment and the native research SDK. It has no application windows,
menus, editor workspace or command-line policy.

| Target | Dependencies and use |
| --- | --- |
| `vultra-research-extension` | Static C++ research library; depends on `vultra` and vendored header-only FLIP. |
| `vultra-research-scripting` | Static experiment ABI adapter; additionally depends on `vultra-scripting`. |
| `vultra-research` | Optional shared Python host in `tools/`; owns the Device and scripting adapter. |
| `vultra-batch` | Standalone offline CLI in `tools/`; uses the same experiment sessions. |
| `vultra-app` | Editor/research frontend in `editor/`; owns its private application code. |

Normal drawing programs and `vultra-runtime` depend on none of these targets. Direct C++ consumers can use
`add_deps("vultra-research-extension")` after including the library's xmake project, including as a subproject.
The scripting adapter and standalone tools are registered only by the root project.

```cpp
#include <vultra/research/experiment_session.hpp>

vultra::ExperimentConfig config;
config.input = "project.vproject";
config.size = {640, 480};
vultra::Device device;
vultra::ExperimentSession session(device, config);
session.render();
vultra::savePfm(session.capture("hdr"), "result.pfm");
```

The Device outlives the session. Rendering completes its GPU submission before returning; captured Images
own their pixels. Script updates precede rendering. Native pass modules stop after their graphs are destroyed
and before the context-owned API tables they borrow expire. Project settings live in `metadata.research`;
the core manifest only preserves metadata and validates its JSON-object boundary. All file and ABI versions
remain `1`, with deliberate breaking changes before release.

The experiment C ABI and Python layouts still come from `scripts/codegen.py` and its single libclang IR.
Normal builds use checked-in generated files. See the [research architecture](../../docs/architecture.md#research-and-delivery),
[build and Python setup](../../BUILD.md#offline-rendering) and [remaining gates](../../docs/roadmap.md#graphics-research).
