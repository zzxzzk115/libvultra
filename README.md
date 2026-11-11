# libvultra

A small C++23 graphics kernel built on [VRI](https://github.com/zzxzzk115/VRI), with readable C++ and Slang code
for research and prototyping. It provides the pieces a host program composes; projects, packages, scripting and
editors belong to the programs built on it.

Everything is one static library, `vultra`. Programs select the modules they need:

| Module | Provides |
| --- | --- |
| `core` | Logging, command line, math, images, typed properties. |
| `platform` | Files, processes, the SDL3 window and input, `FileWatcher`. |
| `gpu` | `Device`, `Frame`, textures, buffers, swapchain, readback, profiling. |
| `assets` | Scene, material and texture values, VPK archives, glTF/GLB, OBJ and FBX importers. |
| `shader` | Slang programs: compile, reflect (including user attributes), cook, load, hot reload. |
| `graph` | A code-driven `RenderGraph` with async compute and Graphviz export, and `.vgraph` definitions. |
| `render` | `GpuScene`, the `RenderPath` interface and the reference `ForwardPath` (`vultra.forward`). |
| `ui` | `EditorGui`: Dear ImGui for a window or an offscreen target. |
| `app` | `DesktopApp`, `ImGuiApp`, Android and Web application loops, camera controllers. |
| `xr` | OpenXR session and swapchain integration. |

A direct GPU program needs no scene, importer, render path or UI.

- [Get started](docs/get_started.md): first programs for a window, an offline render graph and a scene.
- [BUILD.md](BUILD.md): setup, module selection, tools and verification commands.
- [Architecture](docs/architecture.md): modules and concepts. [Roadmap](docs/roadmap.md): open work.
- Dependency revisions and recipes: [external/xmake.lua](external/xmake.lua).

## Quick start

```sh
xmake f -m release -y
xmake build -y --all
xmake run example-basics vri --frames 60
```

On Windows, run xmake from PowerShell or cmd. For a parent xmake project, use `includes("external/libvultra")`
and `add_deps("vultra")`; see [BUILD.md](BUILD.md#use-as-a-subproject).

## Examples

Run `xmake run <target> [mode] [options]`. `--help` lists the compiled modes and options.

| Target | Modes / purpose |
| --- | --- |
| `example-basics` | `window`, `vri`, `graph`, `mesh-shading`. |
| `example-offline` | Windowless triangle, compute pass and display conversion; C++ or `.vgraph` graph; PNG/PFM output. |
| `example-ui` | Raw Dear ImGui and `EditorGui` over a render target. |

Start from the [offline program](examples/offline/offline.cpp) or the [window example](examples/basics/window.cpp).
Examples accept `--frames`, `--log-level` and `--log-file`; windowed ones also accept `--capture <png>`.
[Offline rendering](BUILD.md#offline-rendering) describes automated runs without a desktop window.

## Scope and limitations

- This branch is verified on Windows x64 with Vulkan only. Linux, macOS, Android and Web (WebGL2/WebGPU) code
  paths exist but were not rebuilt after the module restructuring, and no XR runtime or headset was tested.
- SDL3 is the only desktop window backend.
- `ForwardPath` renders static geometry with an opaque OpenPBR material subset, analytic lights and image-based
  lighting. It has no shadows, transparency or mesh shading; other techniques are separate `RenderPath`
  implementations.
- Render graph textures are single-mip, single-layer 2D with whole-resource state tracking.
- All serialized-data and file-format versions are `1` and may change incompatibly before a release.

Remaining work is listed in the [roadmap](docs/roadmap.md).

## Contributing

Follow [AGENTS.md](AGENTS.md), the checked-in formatting and static-analysis rules and the
[development checks](BUILD.md#development-checks). Repository documentation is in English.

## Acknowledgements

- [Adobe OpenPBR BSDF](https://github.com/adobe/openpbr-bsdf): public Slang implementation
- [FileWatch](https://github.com/ThomasMonkman/filewatch): operating-system file-change notifications behind `FileWatcher`
- [VRI](https://github.com/zzxzzk115/VRI): rendering abstraction and Vulkan backend
- [SDL3](https://github.com/libsdl-org/SDL/tree/release-3.4.0): desktop window and input backend (zlib license)
- [Native File Dialog Extended](https://github.com/btzy/nativefiledialog-extended/tree/v1.3.0): Linux portal file dialog (zlib license)
- [Dear ImGui](https://github.com/ocornut/imgui): immediate-mode UI, docking and multiple viewports
- [Slang](https://github.com/shader-slang/slang): shader compilation to SPIR-V and WGSL
- [argparse](https://github.com/p-ranav/argparse): command-line argument parsing
- [spdlog](https://github.com/gabime/spdlog): console and file logging
- [stb](https://github.com/nothings/stb): image loading, SIMD mip filtering and PNG writing
- [NVIDIA FLIP](https://github.com/NVlabs/flip): vendored perceptual image-difference header ([BSD-3-Clause provenance](external/flip/README.vultra.md))
- [VTask](https://github.com/zzxzzk115/vtask): parallel loading, geometry and texture import jobs
- [meshoptimizer](https://github.com/zeux/meshoptimizer): meshlet construction and bounds
- [OpenFBX](https://github.com/nem0/OpenFBX): static FBX scene loading
- [VRI-Framework](https://github.com/zzxzzk115/VRI-Framework): ORCA FBX material and normal-map conventions ([MIT notice](external/vrf_fbx_license.txt))
- [DirectXTex](https://github.com/microsoft/DirectXTex): CPU DDS loading and block decompression
- [TinyObjLoader](https://github.com/tinyobjloader/tinyobjloader): original OBJ/MTL loading
- [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo): direct SIMD BC7 encoding
- [xxHash](https://github.com/Cyan4973/xxHash): source and derived-data content hashes
- [TinyGLTF](https://github.com/syoyo/tinygltf): glTF and GLB scene loading
- [GLM](https://github.com/g-truc/glm): vector and matrix math
- [PCG basic](external/pcg_basic/README.md): random streams (Apache-2.0)
- [RenderDoc API](external/renderdoc/README.md): optional in-application frame capture
- [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK): OpenXR headers and runtime loader
- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers): Vulkan API declarations
- [nlohmann/json](https://github.com/nlohmann/json): asset metadata, graph definitions, input maps and property schemas

## License

libvultra is licensed under the [MIT License](LICENSE). Third-party code and assets retain their own licenses; see [OpenPBR](external/openpbr/README.vultra.md), [FileWatch](external/FileWatch/LICENSE), [BC7 encoder](external/bc7enc/README.md) and [asset attribution](resources/README.md).
