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
`libvultra-research.so` on Linux or `vultra-research.dll` on Windows. Its shaders and OpenPBR sources are embedded.
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

Input may be a model, `.vproject` or `.vpk`. A VPK is extracted into a session-owned temporary directory and removed
after scripts and GPU resources are destroyed. This is file extraction, not resource-stream import. A copied library
and VPK were verified on the Linux development machine; clean-machine and Windows acceptance remain separate gates.

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
frame/pass timing history. Typed Python scene mutation wrappers are not yet exposed.

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
