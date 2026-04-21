# libvultra

<h4 align="center">
  libvultra is the core runtime library of <a href="https://github.com/zzxzzk115/Vultra" target="_blank" rel="noopener noreferrer">Vultra</a>, focused on rapid graphics and game prototyping without requiring VultraEditor.
</h4>

<p align="center">
  <a href="https://github.com/zzxzzk115/libvultra/actions/workflows/build_windows.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/build_windows.yaml?branch=master&label=Build-Windows&logo=github" alt="Build-Windows" />
  </a>
  <a href="https://github.com/zzxzzk115/libvultra/actions/workflows/build_linux.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/build_linux.yaml?branch=master&label=Build-Linux&logo=github" alt="Build-Linux" />
  </a>
  <a href="https://github.com/zzxzzk115/libvultra/actions/workflows/build_macos.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/build_macos.yaml?branch=master&label=Build-macOS&logo=github" alt="Build-macOS" />
  </a>
  <a href="https://github.com/zzxzzk115/libvultra/actions/workflows/build_android.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/build_android.yaml?branch=master&label=Build-Android&logo=github" alt="Build-Android" />
  </a>
  <a href="https://github.com/zzxzzk115/libvultra/actions/workflows/build_wasm.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/build_wasm.yaml?branch=master&label=Build-WASM&logo=github" alt="Build-WASM" />
  </a>
  <a href="https://github.com/zzxzzk115/libvultra/actions/workflows/deploy_pages.yaml">
    <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/libvultra/deploy_pages.yaml?branch=master&label=Deploy-Pages&logo=github" alt="Deploy-Pages" />
  </a>
  <a href="https://www.codefactor.io/repository/github/zzxzzk115/libvultra">
    <img src="https://www.codefactor.io/repository/github/zzxzzk115/libvultra/badge" alt="CodeFactor" />
  </a>
  <a href="https://github.com/zzxzzk115/libvultra/issues">
    <img src="https://img.shields.io/github/issues/zzxzzk115/libvultra" alt="Issues" />
  </a>
  <a href="https://github.com/zzxzzk115/libvultra/blob/master/LICENSE">
    <img src="https://img.shields.io/github/license/zzxzzk115/libvultra" alt="License" />
  </a>
</p>

## Highlights
- New asset pipeline based on `vasset v0.3`.
- Runtime/editor-friendly asset baking and `VPK` packaging workflow.
- New shader pipeline based on `vshadersystem` with rich variants and keyword workflow.
- Write GLSL once and target Vulkan + WebGPU.
- WebGPU rendering now supports both native and Web (WASM / Emscripten).
- Built-in Gaussian Splatting renderer.
- Future direction: GPU-Driven rendering pipeline.

## Rendering / Platform Matrix
- Vulkan: desktop high-end path.
- WebGPU (native): compatibility path.
- WebGPU (WASM / Emscripten): web runtime path.
- Android: compatibility-focused path.

## Showcase
- [GLTF Viewer](./examples/gltf_viewer/main.cpp)
- [Demo App](./examples/demo_app/main.cpp)
- [ImGui (Desktop + WASM)](./examples/imgui/main.cpp)
- [Gaussian Splatting](./examples/gaussian_splatting/main.cpp)

![Example: GLTF Viewer](./media/images/example-gltf-viewer.png)
![Example: Sponza](./media/images/example-sponza.png)

## Build

### Prerequisites
- Git
- XMake
- Vulkan SDK (for Vulkan targets)
- Android SDK + NDK (for Android)
- Emscripten SDK (for WASM)
- Visual Studio (Windows) / Clang or GCC (Linux/macOS)

### Desktop (default)
```bash
git clone --recursive https://github.com/zzxzzk115/libvultra.git
cd libvultra
git submodule update --init --recursive
xmake f -y
xmake build -y
```

### WASM (Emscripten)
```bash
sh scripts/bootstrap_vasset_cli.sh
xmake f -p wasm --libvultra_build_examples=y --libvultra_build_tests=n -y
xmake build -y example-demo-app
```

On Windows PowerShell:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap_vasset_cli.ps1
xmake f -p wasm --libvultra_build_examples=y --libvultra_build_tests=n -y
xmake build -y example-demo-app
```

The bootstrap step installs a host-side `vasset-cli` under `build/.generated/vasset-host/...`.
`example-demo-app` and `example-gaussian-splatting` use it to import assets and package `resources.vpk` for the wasm build.

Output is generated under:
- `build/wasm/wasm32/release/example-demo-app/`

### Android
```bash
xmake f -p android --ndk=/path/to/Android/Sdk/ndk/30.0.14904198 --libvultra_build_examples=n --libvultra_build_tests=n -y
xmake build -y
```

## Run
Run one target:
```bash
xmake run example-demo-app
xmake run example-imgui
xmake run example-gaussian-splatting
```

## Starter Template
Create your own project with:
- [libvultra-starter-template](https://github.com/zzxzzk115/libvultra-starter-template)

For Android host integration reference:
- [`template/android`](./template/android/)
- [`examples/android_app`](./examples/android_app/)

## License
This project is under the [MIT](LICENSE) license.
