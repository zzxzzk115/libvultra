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
  ```

  > **Tips:**
  > For OpenXR programs, you may need to set the XR_RUNTIME_JSON environment variable.
  > For debugging OpenXR programs without headsets, you may need Meta XR Simulator on Windows and macOS. On Linux, you can use Monado as the simulator.

## Future Work
- [x] Wayland support
- [ ] More powerful texture loader that supports KTX, KTX2, DDS and more
  - [x] KTX
  - [ ] KTX2
  - [x] DDS
- [ ] ECS-based scene management with EnTT
- [ ] Modern Render Paths
- [ ] Raytracing Pipeline
- [ ] Mesh Shading Pipeline
- [ ] Resource Pipeline
- [ ] Lua or C# Scripting System
- [ ] (Maybe?) AR support

## License
This project is under the [MIT](LICENSE) license.
