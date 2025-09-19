# libvultra

<h4 align="center">
  libvultra is the core library of Vultra, which can be used for rapidly creating graphics or game prototypes without the VultraEditor.
</h4>

<p align="center">
    <a href="https://github.com/zzxzzk115/libvultra/actions" alt="Build-Windows">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/build_windows.yaml?branch=master&label=Build-Windows&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/libvultra/actions" alt="Build-Linux">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/build_linux.yaml?branch=master&label=Build-Linux&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/libvultra/actions" alt="Build-macOS">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/build_macos.yaml?branch=master&label=Build-macOS&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/libvultra/issues" alt="GitHub Issues">
        <img src="https://img.shields.io/github/issues/zzxzzk115/libvultra">
    </a>
    <a href="https://www.codefactor.io/repository/github/zzxzzk115/libvultra"><img src="https://www.codefactor.io/repository/github/zzxzzk115/libvultra/badge" alt="CodeFactor" /></a>
    <a href="https://github.com/zzxzzk115/libvultra/blob/master/LICENSE" alt="GitHub">
        <img src="https://img.shields.io/github/license/zzxzzk115/libvultra">
    </a>
</p>

(This project is under early development and WIP.)

## Features
- Modern Vulkan using Vulkan-Hpp, Vulkan-Memory-Allocator-Hpp and more
- FrameGraph (RenderGraph) based rendering system
- OpenXR support (now focusing on VR only, not AR)
- Modern SDL using SDL3
- ImGui docking + multiview

## Showcase
[Example: GLTF Viewer](./examples/gltf_viewer/main.cpp)

![Example: GLTF Viewer](./media/images/example-gltf-viewer.png)

## Build Instructions

Prerequisites:
- Git
- XMake
- Vulkan SDK
- Visual Studio with MSVC if Windows
- GCC or Clang if Linux/Unix
- XCode with GCC or Apple Clang if macOS

Step-by-Step:

- Install XMake by following [this](https://xmake.io/guide/quick-start.html#installation). 

- Clone the project:
  ```bash
  git clone https://github.com/zzxzzk115/libvultra.git
  ```

- Build the project:
  ```bash
  cd libvultra
  xmake -vD
  ```

- Run the programs:
  ```bash
  xmake run
  ```
  or run a specific program:
  ```bash
  xmake run example-window
  xmake run example-rhi-triangle
  xmake run example-imgui
  xmake run example-framegraph-triangle
  xmake run example-openxr-triangle
  xmake run example-gltf-viewer
  ```

  > **Tips:**
  > For OpenXR programs, you may need to set the XR_RUNTIME_JSON environment variable.
  > For debugging OpenXR programs without headsets, you may need Meta XR Simulator on Windows and macOS. On Linux, you can use Monado as the simulator.

## TODO List
- [x] Wayland support
- [x] More powerful texture loader that supports KTX, KTX2, DDS and more
  - [x] KTX
  - [x] KTX2
  - [x] DDS
  - [x] EXR
- [x] ECS-based scene management with EnTT
- [ ] Raytracing Pipeline
- [ ] Mesh Shading Pipeline
- [ ] Resource Pipeline
- [ ] Lua or C# Scripting System
- [ ] (Maybe?) AR support

## Create your own graphics or game project
You can simply create a project by using this [template](https://github.com/zzxzzk115/libvultra-starter-template).

Have fun!

## License
This project is under the [MIT](LICENSE) license.
