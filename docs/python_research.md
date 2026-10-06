# Offline research sessions

`vultra-batch` and the optional Python library use `ExperimentSession`, the existing renderer and RenderGraph.
Neither path constructs a window, swapchain or GUI. A project session runs native C++, Lua and C# updates at a
fixed time step before synchronizing the scene and recording GPU work. GUI callbacks and hot reload are disabled.
Lua is static; native modules remain files and C# requires the installed .NET 10 runtime.

Build the optional shared host and install the small Python package:

```sh
xmake build vultra-research vultra-batch
python -m pip install ./source/api/python
```

Building needs xmake. Running the built host and installed package does not. Pass the host library explicitly:
`libvultra-research.so` on Linux or `vultra-research.dll` on Windows. Its cooked built-in SPIR-V shaders and OpenPBR attribution are embedded; the sample project compute pass remains embedded Slang source.
NumPy is a Python dependency and is not linked into `vultra` or the packaged player.

```python
import json
from pathlib import Path
import numpy as np
from vultra import Research

graph = json.loads(Path("examples/research/color_gain.vgraph").read_text())
with Research("build/linux/x86_64/release/libvultra-research.so") as research:
    with research.open("resources/research_lighting.vproject", width=640, height=480) as session:
        session.set_graph(graph)
        for gain in (0.25, 0.5, 1.0):
            session.set_parameter("gain", "gain", gain)
            session.step(2)
            np.save(f"gain-{gain}.npy", session.image("hdr"))
        print(session.progress)
        print(session.scene["version"])
```

Input may be a model, `.vproject` or `.vpk`. A VPK stays mounted in a session-owned `AssetSource`: scenes, models,
external model dependencies, HDR environments, textures and cooked game shaders are read from checked entry bytes.
Script files and native/managed sidecars are materialized individually and removed after their script hosts stop.
The embedded engine shader pack still uses the shared file-based bootstrap. Direct-entry loading, copied
library/VPK startup and CLI/Python raster/reference readbacks pass on Linux. Earlier Windows delivery checks
predate this source migration. Current Windows Vulkan checks also cover copied-library/VPK loading and exact
CLI/Python raster/reference parity; separate clean-machine delivery, D3D12 and cross-device baselines remain open.

`image()` returns display-encoded final color. `image("hdr")` returns the processed linear scene color. Other names
must be marked graph outputs, such as `gain.color`. Returned arrays have shape `(height, width, 4)`, `float32`, a
top-left origin and their own contiguous storage. They remain valid after closing the session and host. No implicit
gamma conversion, quantization or NumPy view into native memory occurs.

Use context managers or explicit `close()`. A session retains its host; host closure closes all live sessions.
Calls must remain on the thread that created the host, including closure. Use separate processes for concurrent
scans because shader lookup currently changes the process working directory during native calls. Invalid graph
replacement preserves the last valid graph and completed image. A valid parameter change affects the next `step()`;
readback before that step still represents the preceding completed frame.

The generated version-1 experiment function table is derived from annotated C++ `ExperimentHost` methods by the
same libclang/IR generator as the scene API. Only device/library bootstrap and Python ownership are handwritten.
Stale version-1 table sizes, foreign session IDs, invalid buffers and closed handles are rejected. Native exceptions
become status codes and an operation-specific UTF-8 error. Normal builds do not require libclang or Python.

For direct C++ research, `ExperimentSession` requires only `add_deps("vultra")`; callers may update `scene()` before
`render()`. `ExperimentHost` adds the optional script lifecycle and generated ABI through `vultra-scripting`.

CLI reports associate images with build/device identifiers, input and shader hashes, graph parameters, pass timings,
fixed time step and final scene state. `--warmup` frames advance scripts and rendering but are excluded from timing
statistics. Python `session.report` exposes completed-frame device/graph/resource/producer and nested timing data;
`session.save_report(new_directory, revision="BUILD_ID", outputs=("hdr", "scene.depth"))` exports selected linear RGB
PFMs and the version-1 graph report. Python reports contain the last completed frame rather than the CLI's full
frame/pass timing history. Both reports include `provenance`: build mode and Slang toolchain, the current graph
and scene snapshot, entry/declared asset/environment hashes, actual importer-consumed model/buffer/texture dependency
hashes, and hashes of available engine shader artifacts/libraries. Dependency hashes survive cache hits. Hashing
runs only when a report is requested, outside measured frames. `revision` must identify the retained source/build;
the report cannot recover arbitrary unrecorded script RNG or external state. Declared file hashes are read at report
time; importer hashes describe the bytes actually consumed. Retain the files alongside the experiment.

## Typed scene edits and AOV inspection

Typed settings are generated from the same annotated C++ methods, reflected defaults and POD layouts as the native
scene API. They do not introduce another schema or registry. Project/VPK sessions use persistent UUID strings;
direct model sessions have no SceneTree and reject scene-edit operations. Returned frozen dataclasses hold owned
values; setters validate both numeric storage and the scene's semantic constraints before publishing revisions.

```python
from dataclasses import replace

with Research(library) as research, research.open(project, width=640, height=480) as session:
    camera = session.find_node("Camera")  # Requires exactly one matching name.
    settings = session.camera_settings(camera)
    session.set_camera_settings(camera, replace(settings, vertical_fov=0.75))
    transform = session.transform(camera)  # Owned 4×4 float32 matrix, ordinary row/column indexing.
    transform[0, 3] += 0.1
    session.set_transform(camera, transform)
    environment = session.scene["current_environment"]
    session.set_environment_settings(environment, replace(session.environment_settings(environment), intensity=0.5))
    material = session.scene["materials"][0]["id"]
    session.set_material_parameters(material, replace(session.material_parameters(material), coat_weight=0.5))
    session.step()
    raw = session.image("hdr")
    display = session.preview("hdr", channel="luminance", minimum=0, maximum=8)
    pixel = session.probe_pixel("hdr", 320, 240)  # Original float RGBA, without display mapping.
```

`light_settings()` / `set_light_settings()` expose color, intensity, range and spot cones. Camera, light,
environment and material values reject wrong node/resource kinds and non-finite inputs. Transforms must be finite,
invertible affine matrices. The matrix ABI uses 16 column-major floats internally; Python performs the conversion.
Changes become visible on the next `step()`; prior owned arrays remain valid. Numeric edits retain graph textures
and geometry. Progressive reference history resets when the affected rendered state changes.

`preview()` returns a separate owned float32 RGBA display array, with RGB clamped to the declared range and alpha
one. Channels are `rgb`, `r`, `g`, `b`, `a`, and `luminance`. Invalid or equal range endpoints are rejected.
`image()`, pixel probes and PFM exports retain signed/HDR values. Probe coordinates use the top-left origin.

## Built-in stage graphs

Load `examples/research/deferred.vgraph` to compose the shared shadow, skybox, G-buffer and deferred lighting stages
explicitly. Such a graph selects the indexed deferred contract and replaces the automatic scene prelude. Keeping
only G-buffer outputs culls unused lighting and shadows. A regular post-processing graph still receives
`scene.hdr`; deferred sessions additionally expose seven `scene.GBUFFER_NAME` ports, `scene.depth` and
`scene.cascade0` through `scene.cascade3`. See the [pass contracts](guide.md#built-in-raster-pass-composition).
Failed stage/format/extent edits retain the last valid graph and completed image.

`resources/research.vexperiment` is a version-1 reproducible experiment input. It contains the model/project/VPK path,
extent, render path, seed, warmup/measured frames, fixed time step, an optional environment override, an inline graph
and an optional pinned camera. Paths resolve relative to the file. A `null` camera uses the project's selected camera
or the deterministic bounds-based orbit; a camera object stores column-major `view`/`projection` matrices and positive
`near`/`far` planes, using Vultra's right-handed, Y-up, depth-[0,1] camera convention. A pinned camera takes precedence
over scene scripts. Import-cache paths remain local to the host.

```sh
./build/linux/x86_64/release/vultra-batch --experiment resources/research.vexperiment \
    --revision YOUR_BUILD_ID --output build/.tmp/baseline
```

Every batch output includes an effective `experiment.vexperiment` that can be run again. `--experiment` is mutually
exclusive with flags that alter its render/simulation inputs; output location, revision, comparison and capture options
remain command-line choices. Assets and deterministic scripts must still be retained; the description itself is not
an asset archive. Both descriptions and generated ABI remain version `1` before release.

```python
with Research("build/linux/x86_64/release/libvultra-research.so") as research:
    with research.run("resources/research.vexperiment") as session:
        print(session.progress)  # Includes warmup frames, as scripts advance during warmup.
        np.save("baseline.npy", session.image("hdr"))
```

For progressive reference transport, construct `Research(library, ray_tracing=True)` and open a session with
`path="reference", seed=42`, or run a description that selects that path. This explicitly requests VRI ray query and
bindless capabilities. Surface/radiance/sample/ray AOVs are ordinary `session.image("scene.PORT")` names. See
[reference rendering](reference_renderer.md) for their units, material limits, motion semantics and history resets.
Python and CLI reference sessions have exact same-device HDR readback parity. Deterministic runs still require
scripts with deterministic RNG/time behavior; the transport seed does not seed language VMs.

Acceptance commands (Linux names shown):

```sh
env -u DISPLAY -u WAYLAND_DISPLAY PYTHONPATH=source/api/python \
    python tests/research_python.py build/linux/x86_64/release/libvultra-research.so \
    build/linux/x86_64/release/vultra-batch
python tests/offline_render.py build/linux/x86_64/release/vultra-batch
xmake codegen --check
```
